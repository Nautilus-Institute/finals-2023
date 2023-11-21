use std::fs::File;
use std::io::Read;
use std::io::Write;

use anyhow::{bail, Result};

use nix::sys::signal::Signal;

use pete::ptracer::{Pid, Ptracer, Tracee, Restart, Stop, Options};

use crate::tracer::{Tracer, TracerError};

pub struct SyslessTracer {
    pub tracee: Tracee,
    stdin: File,
    stdout: File,
}

// Describes a ptrace-less tracer. Uses access to mem and sigaltstack 
// to access underlying process
impl SyslessTracer {
    pub fn new(pid: u32, stdin: File, stdout: File) -> Self {

        let pete_pid = Pid::from_raw(pid as i32);

        let mut ptracer = Ptracer::new();
        if ptracer.attach(pete_pid).is_err() {
            panic!("failed to attach to child process {pid}");
        }

        let mut tracee = wait_until_exec(&mut ptracer).expect("child to exec");

        let mut options = Options::all();
        options.remove(Options::PTRACE_O_TRACEFORK);
        options.remove(Options::PTRACE_O_TRACEVFORK);
        options.remove(Options::PTRACE_O_TRACEEXEC);
        options.remove(Options::PTRACE_O_TRACEEXIT);
        options.remove(Options::PTRACE_O_TRACESECCOMP);
        tracee.set_options(options).expect("set options");

        ptracer.restart(tracee, Restart::Continue).expect("restart child");

        Self { stdin, stdout, tracee }
    }
}

impl Tracer for SyslessTracer {
    fn write_out(&mut self, buf: &[u8]) -> Result<usize, TracerError> {
        Ok(self.stdin.write(buf).map_err(|err| TracerError::Io(err))?)
    }

    fn read_in(&mut self, buf: &mut [u8]) -> Result<usize, TracerError> {
        Ok(self.stdout.read(buf).map_err(|err| TracerError::Io(err))?)
    }
}

fn wait_until_exec(tracer: &mut Ptracer) -> Result<Tracee> {
    while let Some(tracee) = tracer.wait()? {
        if let Stop::SignalDelivery{ signal } = &tracee.stop {
            if *signal == Signal::SIGTRAP {
                return Ok(tracee);
            }
        }

        tracer.restart(tracee, Restart::Continue)?;
    }
    bail!("never saw exec");
}
