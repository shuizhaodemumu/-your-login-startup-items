//! Native watchdog. All destructive operations use retained process/job handles,
//! never a process name or a recycled PID. No KILL_ON_JOB_CLOSE: exiting the UI
//! deliberately leaves managed applications running.
use std::{collections::VecDeque, ffi::c_void, mem::{size_of, zeroed}, ptr::{null, null_mut}, sync::Mutex, time::{Duration, Instant}};
type Handle = isize;
const HANG_LIMIT: Duration = Duration::from_secs(10);
const MULTIPLE: u32 = 100001;
const CIRCUIT: u32 = 100002;
const STOP_TIMEOUT: u32 = 100003;

#[repr(C)] #[derive(Clone, Copy, Default, Debug)] pub struct Point { pub x:i32, pub y:i32 }
#[repr(C)] #[derive(Clone, Copy, Default, Debug)] pub struct Rect { pub left:i32, pub top:i32, pub right:i32, pub bottom:i32 }
#[repr(C)] #[derive(Clone, Copy, Default, Debug)] pub struct Placement { pub length:u32, pub flags:u32, pub show:u32, pub min:Point, pub max:Point, pub rect:Rect }
#[repr(C)] struct Startup { cb:u32, reserved:*mut u16, desktop:*mut u16, title:*mut u16, x:u32,y:u32,xsize:u32,ysize:u32,xchars:u32,ychars:u32,fill:u32,flags:u32,show:u16,cbreserved:u16,reserved2:*mut u8,stdin:Handle,stdout:Handle,stderr:Handle }
#[repr(C)] struct ProcessInfo { process:Handle, thread:Handle, pid:u32, tid:u32 }
#[repr(C)] struct ProcessEntry { size:u32,usage:u32,pid:u32,heap:usize,module:u32,threads:u32,parent:u32,priority:i32,flags:u32,exe:[u16;260] }
#[repr(C)] #[derive(Clone, Copy, Default)] pub struct Snapshot { pub state:u32, pub pid:u32, pub restarts:u32, pub hung_ms:u32, pub error:u32, pub reason:u32, pub has_placement:u32, pub placement:Placement }
#[link(name="kernel32")] extern "system" {
 fn CloseHandle(h:Handle)->i32; fn GetLastError()->u32; fn SetLastError(e:u32);
 fn CreateProcessW(app:*const u16,cmd:*mut u16,pa:*const c_void,ta:*const c_void,inherit:i32,flags:u32,env:*const c_void,cwd:*const u16,si:*const Startup,pi:*mut ProcessInfo)->i32;
 fn ResumeThread(h:Handle)->u32; fn TerminateProcess(h:Handle,exit:u32)->i32;
 fn WaitForSingleObject(h:Handle,ms:u32)->u32;
 fn CreateJobObjectW(a:*const c_void,n:*const u16)->Handle; fn AssignProcessToJobObject(job:Handle,process:Handle)->i32;
 fn TerminateJobObject(job:Handle,exit:u32)->i32; fn IsProcessInJob(process:Handle,job:Handle,result:*mut i32)->i32;
 fn QueryInformationJobObject(job:Handle,class:u32,info:*mut c_void,len:u32,ret:*mut u32)->i32;
 fn OpenProcess(access:u32,inherit:i32,pid:u32)->Handle;
 fn QueryFullProcessImageNameW(h:Handle,flags:u32,name:*mut u16,size:*mut u32)->i32;
 fn CreateToolhelp32Snapshot(flags:u32,pid:u32)->Handle; fn Process32FirstW(h:Handle,e:*mut ProcessEntry)->i32; fn Process32NextW(h:Handle,e:*mut ProcessEntry)->i32;
 fn ProcessIdToSessionId(pid:u32,s:*mut u32)->i32; fn GetCurrentProcessId()->u32;
}
#[link(name="user32")] extern "system" {
 fn EnumWindows(cb:unsafe extern "system" fn(Handle,isize)->i32,data:isize)->i32;
 fn IsWindowVisible(h:Handle)->i32; fn GetWindowThreadProcessId(h:Handle,pid:*mut u32)->u32;
 fn SendMessageTimeoutW(h:Handle,msg:u32,w:usize,l:isize,flags:u32,timeout:u32,result:*mut usize)->isize;
 fn GetWindowPlacement(h:Handle,p:*mut Placement)->i32; fn SetWindowPlacement(h:Handle,p:*const Placement)->i32;
 fn GetWindow(h:Handle,cmd:u32)->Handle;
}
struct Owned(Handle);
impl Drop for Owned { fn drop(&mut self) { if self.0!=0 && self.0 != -1 { unsafe { CloseHandle(self.0); } } } }
fn wide(s:&str)->Vec<u16> { s.encode_utf16().chain(Some(0)).collect() }
unsafe fn read_wide(p:*const u16)->String { if p.is_null(){return String::new()} let mut n=0; while *p.add(n)!=0 && n<32768 {n+=1;} String::from_utf16_lossy(std::slice::from_raw_parts(p,n)) }
fn error()->u32 { unsafe { GetLastError() } }
fn process_path(h:Handle)->Option<String> { let mut b=vec![0u16;32768]; let mut n=b.len() as u32; if unsafe { QueryFullProcessImageNameW(h,0,b.as_mut_ptr(),&mut n) }==0 {None} else {Some(String::from_utf16_lossy(&b[..n as usize]))} }
fn same_path(a:&str,b:&str)->bool {a.replace('/',"\\").to_lowercase()==b.replace('/',"\\").to_lowercase()}

/// Same-image descendants are grouped under their ancestor (e.g. Chromium).
/// Multiple independent roots are ambiguous and must never be picked at random.
fn existing(path:&str)->Result<Option<(Owned,u32)>,u32> { unsafe {
 let snap=Owned(CreateToolhelp32Snapshot(2,0)); if snap.0 == -1 { return Err(error()) }
 let mut e:ProcessEntry=zeroed(); e.size=size_of::<ProcessEntry>() as u32;
 let mut session=0; ProcessIdToSessionId(GetCurrentProcessId(),&mut session);
 let mut matches=Vec::new(); let mut ok=Process32FirstW(snap.0,&mut e);
 let name=std::path::Path::new(path).file_name().unwrap_or_default().to_string_lossy().to_lowercase();
 while ok!=0 {
  let end=e.exe.iter().position(|v|*v==0).unwrap_or(260);
  if String::from_utf16_lossy(&e.exe[..end]).to_lowercase()==name && e.pid!=GetCurrentProcessId() {
   let mut s=0; if ProcessIdToSessionId(e.pid,&mut s)!=0 && s==session {
    let query=Owned(OpenProcess(0x1000,0,e.pid));
    if query.0==0 { return Err(error()) }
    if process_path(query.0).as_deref().map(|p|same_path(path,p)).unwrap_or(false) {matches.push((e.pid,e.parent));}
   }
  }
  ok=Process32NextW(snap.0,&mut e);
 }
 let roots:Vec<u32>=matches.iter().filter(|(_,parent)|!matches.iter().any(|(pid,_)|pid==parent)).map(|(p,_)|*p).collect();
 if roots.len()>1 {return Err(MULTIPLE)}
 if let Some(pid)=roots.first() { let h=Owned(OpenProcess(0x100000|0x1000|1,0,*pid)); if h.0==0{return Err(error())} if WaitForSingleObject(h.0,0)==0{return Ok(None)}
  // Revalidate after opening the retained handle: the snapshot PID may have exited/recycled.
  if !process_path(h.0).as_deref().map(|p|same_path(path,p)).unwrap_or(false){return Err(100004)}
  return Ok(Some((h,*pid))); }
 Ok(None)
} }

#[derive(Default)] struct HangClock { since:Option<Instant>, last:Option<Instant> }
impl HangClock {
 fn update(&mut self,now:Instant,hung:bool)->Duration {
  // A long scheduling/sleep gap is not evidence of continuous unresponsiveness.
  if self.last.map(|t|now.duration_since(t)>Duration::from_secs(3)).unwrap_or(false) {self.since=None;}
  self.last=Some(now);
  if !hung {self.since=None;return Duration::ZERO}
  now.duration_since(*self.since.get_or_insert(now))
 }
}
pub struct Guard { path:String,args:String,cwd:String,process:Option<Owned>,job:Option<Owned>,pid:u32,enabled:bool,clock:HangClock,snapshot:Snapshot,attempts:VecDeque<Instant>,restore_until:Option<Instant>,last_tick:Instant }
impl Guard {
 fn new(path:String,args:String,cwd:String)->Self {Self{path,args,cwd,process:None,job:None,pid:0,enabled:true,clock:HangClock::default(),snapshot:Snapshot::default(),attempts:VecDeque::new(),restore_until:None,last_tick:Instant::now()}}
 fn fail(&mut self,err:u32) {self.snapshot.state=5;self.snapshot.error=err;self.clock=HangClock::default();}
 fn alive(&self)->bool { self.process.as_ref().map(|h|unsafe{WaitForSingleObject(h.0,0)==258}).unwrap_or(false) }
 fn launch(&mut self,attach:bool)->Result<(),u32> {
  if self.alive() {return Ok(())}
  if attach { if let Some((h,pid))=existing(&self.path)? {self.process=Some(h);self.job=None;self.pid=pid;self.snapshot.pid=pid;self.snapshot.reason=2;self.snapshot.state=1;return Ok(())} }
  // Never inherit shell parsing; the application path is explicit and quoted.
  let path=wide(&self.path);let cwd=wide(&self.cwd);let mut cmd=wide(&format!("\"{}\" {}",self.path,self.args));
  unsafe {
   let job=Owned(CreateJobObjectW(null(),null())); if job.0==0{return Err(error())}
   let mut si:Startup=zeroed();si.cb=size_of::<Startup>() as u32;
   let mut pi:ProcessInfo=zeroed();
   if CreateProcessW(path.as_ptr(),cmd.as_mut_ptr(),null(),null(),0,4,null(),cwd.as_ptr(),&si,&mut pi)==0 {return Err(error())}
   let process=Owned(pi.process);let thread=Owned(pi.thread);
   // Assign while suspended, so descendants cannot escape between creation and assignment.
   if AssignProcessToJobObject(job.0,process.0)==0 {let err=error();TerminateProcess(process.0,1);WaitForSingleObject(process.0,3000);return Err(err)}
   if ResumeThread(thread.0)==u32::MAX {let err=error();TerminateJobObject(job.0,1);return Err(err)}
   self.process=Some(process);self.job=Some(job);self.pid=pi.pid;self.snapshot.pid=pi.pid;
  }
  self.snapshot.state=1;self.snapshot.error=0;self.clock=HangClock::default();self.restore_until=Some(Instant::now()+Duration::from_secs(20));Ok(())
 }
 fn terminate(&mut self)->Result<(),u32> { unsafe {
  if let Some(job)=self.job.as_ref() { if TerminateJobObject(job.0,1)==0{return Err(error())} }
  else if let Some(p)=self.process.as_ref() { if self.alive() && TerminateProcess(p.0,1)==0 {return Err(error())} }
  if let Some(p)=self.process.as_ref(){ if WaitForSingleObject(p.0,3000)!=0{return Err(STOP_TIMEOUT)} }
  if let Some(job)=self.job.as_ref() {
   let start=Instant::now();loop { // JOBOBJECT_BASIC_ACCOUNTING_INFORMATION (48 bytes).
    let mut data=[0u64;6];if QueryInformationJobObject(job.0,1,data.as_mut_ptr() as *mut c_void,48,null_mut())==0{return Err(error())}
    let active=*((data.as_ptr() as *const u8).add(40) as *const u32);
    if active==0{break} if start.elapsed()>Duration::from_secs(3){return Err(STOP_TIMEOUT)} std::thread::sleep(Duration::from_millis(30));
   }
  }
  self.process=None;self.job=None;self.pid=0;self.snapshot.pid=0;Ok(())
 } }
 fn restart(&mut self,reason:u32)->Result<(),u32> {
  let now=Instant::now();while self.attempts.front().map(|t|now.duration_since(*t)>Duration::from_secs(60)).unwrap_or(false){self.attempts.pop_front();}
  // Stop residual child processes even when the circuit breaker is reached.
  self.terminate()?;
  if self.attempts.len()>=5 {return Err(CIRCUIT)}
  self.attempts.push_back(now);self.snapshot.reason=reason;self.launch(false)?;self.snapshot.restarts+=1;Ok(())
 }
 fn windows(&self)->Vec<Handle> {
  struct Context {pid:u32,job:Handle,windows:Vec<Handle>}
  unsafe extern "system" fn cb(w:Handle,data:isize)->i32 {let c=&mut *(data as *mut Context);if IsWindowVisible(w)==0{return 1}let mut pid=0;GetWindowThreadProcessId(w,&mut pid);let mut belongs=pid==c.pid;
   if !belongs && c.job!=0 {let p=Owned(OpenProcess(0x1000,0,pid));let mut yes=0;if p.0!=0&&IsProcessInJob(p.0,c.job,&mut yes)!=0 {belongs=yes!=0;}}
   if belongs {c.windows.push(w);}1
  }
  let mut c=Context{pid:self.pid,job:self.job.as_ref().map(|j|j.0).unwrap_or(0),windows:Vec::new()};unsafe{EnumWindows(cb,&mut c as *mut _ as isize);}c.windows
 }
 fn tick(&mut self) {
  let now=Instant::now();self.last_tick=now;
  if self.snapshot.state==5{return}
  if !self.alive() {
   self.clock=HangClock::default();self.snapshot.hung_ms=0;
   if self.enabled && self.process.is_some() {if let Err(e)=self.restart(3){self.fail(e)}}
   else {self.snapshot.state=0;self.snapshot.pid=0;}
   return
  }
  let windows=self.windows();let mut hung=false;
  for w in &windows {unsafe{let mut result=0;SetLastError(0);if SendMessageTimeoutW(*w,0,0,0,0x23,200,&mut result)==0 {
   let err=error();if err==5 {self.fail(err);return} // UIPI denial isn't a hang.
   // A destroyed window during the probe is not evidence of a hung process.
   let mut pid=0;if GetWindowThreadProcessId(*w,&mut pid)!=0 {hung=true;break;}
  }}}
  let elapsed=self.clock.update(Instant::now(),hung);self.snapshot.hung_ms=elapsed.as_millis().min(u32::MAX as u128) as u32;
  if hung {self.snapshot.state=if self.enabled{2}else{4};if self.enabled&&elapsed>=HANG_LIMIT {if let Err(e)=self.restart(4){self.fail(e)}}return}
  self.snapshot.state=if self.enabled{1}else{4};
  let main=windows.iter().find(|w|unsafe{GetWindow(**w,4)==0}).or_else(||windows.first());
  if let Some(w)=main {unsafe {
   if self.restore_until.is_some() && self.snapshot.has_placement!=0 {
    if self.restore_until.unwrap()>now {if SetWindowPlacement(*w,&self.snapshot.placement)!=0{self.restore_until=None;}} else {self.restore_until=None;}
   } else {let mut p=Placement::default();p.length=size_of::<Placement>() as u32;if GetWindowPlacement(*w,&mut p)!=0 && p.rect.right>p.rect.left && p.rect.bottom>p.rect.top {self.snapshot.placement=p;self.snapshot.has_placement=1;self.restore_until=None;}}
  }}
 }
}

#[no_mangle] pub unsafe extern "C" fn dg_create(path:*const u16,args:*const u16,cwd:*const u16)->*mut Mutex<Guard>{Box::into_raw(Box::new(Mutex::new(Guard::new(read_wide(path),read_wide(args),read_wide(cwd)))))}
#[no_mangle] pub unsafe extern "C" fn dg_free(g:*mut Mutex<Guard>){if !g.is_null(){drop(Box::from_raw(g));}}
#[no_mangle] pub unsafe extern "C" fn dg_start(g:*mut Mutex<Guard>)->u32 {let mut g=(*g).lock().unwrap();g.attempts.clear();g.snapshot.error=0;g.snapshot.state=0;g.snapshot.reason=1;match g.launch(true){Ok(())=>0,Err(e)=>{g.fail(e);e}}}
#[no_mangle] pub unsafe extern "C" fn dg_tick(g:*mut Mutex<Guard>,s:*mut Snapshot){let mut g=(*g).lock().unwrap();g.tick();*s=g.snapshot;}
#[no_mangle] pub unsafe extern "C" fn dg_snapshot(g:*mut Mutex<Guard>,s:*mut Snapshot){*s=(*g).lock().unwrap().snapshot;}
#[no_mangle] pub unsafe extern "C" fn dg_enable(g:*mut Mutex<Guard>,enabled:i32){let mut g=(*g).lock().unwrap();g.enabled=enabled!=0;g.clock=HangClock::default();g.snapshot.hung_ms=0;if g.snapshot.state!=5&&g.alive(){g.snapshot.state=if g.enabled{1}else{4};}}
#[no_mangle] pub unsafe extern "C" fn dg_configure(g:*mut Mutex<Guard>,args:*const u16,cwd:*const u16){let mut g=(*g).lock().unwrap();g.args=read_wide(args);g.cwd=read_wide(cwd);}
#[no_mangle] pub unsafe extern "C" fn dg_restore(g:*mut Mutex<Guard>,p:*const Placement){let mut g=(*g).lock().unwrap();if !p.is_null() && (*p).length==size_of::<Placement>() as u32{g.snapshot.placement=*p;g.snapshot.has_placement=1;}}
#[no_mangle] pub unsafe extern "C" fn dg_restart(g:*mut Mutex<Guard>)->u32 {let mut g=(*g).lock().unwrap();g.attempts.clear();g.snapshot.error=0;match g.restart(5){Ok(())=>0,Err(e)=>{g.fail(e);e}}}

#[cfg(test)] mod tests {
 use super::*;
 #[test] fn requires_ten_observed_seconds(){let t=Instant::now();let mut c=HangClock::default();for i in 0..10 {assert!(c.update(t+Duration::from_secs(i),true)<HANG_LIMIT)}assert_eq!(c.update(t+Duration::from_secs(10),true),HANG_LIMIT);}
 #[test] fn response_resets_countdown(){let t=Instant::now();let mut c=HangClock::default();for i in 0..9{c.update(t+Duration::from_secs(i),true);}c.update(t+Duration::from_secs(9),false);assert_eq!(c.update(t+Duration::from_secs(10),true),Duration::ZERO);}
 #[test] fn sleep_is_not_a_hang(){let t=Instant::now();let mut c=HangClock::default();c.update(t,true);assert_eq!(c.update(t+Duration::from_secs(60),true),Duration::ZERO);}
 #[test] fn native_layout(){assert_eq!(size_of::<Placement>(),44);assert_eq!(size_of::<Snapshot>(),72);assert_eq!(size_of::<ProcessEntry>(),568);}
 #[test] fn paths_case_insensitive(){assert!(same_path("D:/Apps/Test.exe","d:\\apps\\test.EXE"));assert!(!same_path("D:/A/Test.exe","D:/B/Test.exe"));}
}
