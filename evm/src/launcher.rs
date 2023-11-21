use std::{
    fs,
    ptr,
    thread,
    fs::File,
    error::Error,
    fs::remove_dir_all,
    env::set_current_dir,
    io::{stdin, Read, Write},
    os::fd::FromRawFd,
    path::{Path, PathBuf},
    process::exit,
};

use std::io::Result as IoResult;
use std::io::Error as IoError;
use std::io::ErrorKind as IoErrorKind;

use libc::{fexecve, c_char};

use hex;
use nix::{
    unistd,
};
use rlimit;
use log::info;
use seccompiler;
use tempdir::TempDir;
use rand::{RngCore, rngs::OsRng};
use signal_hook::iterator::Signals;
use signal_hook::consts::{SIGINT, SIGALRM};
use landlock::{self, RulesetAttr, RulesetCreatedAttr, Access};

use crate::FLAG_LEN;
use crate::sysless_tracer;

#[cfg(feature = "game_1")]
use crate::game_1::GameTracer as Tracer;
#[cfg(feature = "game_2")]
use crate::game_2::GameTracer as Tracer;
#[cfg(feature = "game_3")]
use crate::game_3::GameTracer as Tracer;

pub fn read_bytecode_stdin() -> Result<Vec<u8>, Box<dyn Error>> {
    let mut line = String::new();

    stdin().read_line(&mut line)?;

    let line = line.trim_end();

    let bytes = hex::decode(line)?;

    Ok(bytes)
}

pub fn read_bytecode(bytecode_path: Option<PathBuf>)
     -> Result<Vec<u8>, Box<dyn Error>> {

    if let Some(bytecode_file) = bytecode_path {
        let mut file = File::open(bytecode_file)?;

        let mut contents = Vec::new();
        file.read_to_end(&mut contents)?;

        Ok(contents)
    } else {
        read_bytecode_stdin()
    }
}

pub fn load_challenge(challenge_path: &str) -> IoResult<Vec<u8>> {
    let mut file = File::open(challenge_path)?;
    info!("loading challenge {}", challenge_path);

    let mut contents = Vec::new();
    file.read_to_end(&mut contents)?;

    Ok(contents)
}

fn generate_flag_data() -> [u8; FLAG_LEN] {
    let mut flag_data = [0u8; FLAG_LEN];
    OsRng.fill_bytes(&mut flag_data);

    flag_data
}

fn instantiate_execution_dir(flag_data: &[u8], artifacts: Vec<PathBuf>)
     -> Result<TempDir, IoError> {
    let tmp_dir = TempDir::new("evm")?;

    let flag_path = tmp_dir.path().join("flag");
    let mut tmp_file = File::create(flag_path)?;

    tmp_file.write(flag_data)?;

    for artifact_path in artifacts {
        let dest_artifact_path = tmp_dir.path().join(
            artifact_path.file_name().expect("path to file provided")
        );

        fs::copy(artifact_path, dest_artifact_path)?;
    }

    Ok(tmp_dir)
}

pub fn create_execution_env(artifacts: Vec<PathBuf>)
     -> Result<(TempDir, [u8; FLAG_LEN]), IoError> {
    let flag_data = generate_flag_data();

    Ok((instantiate_execution_dir(&flag_data[..], artifacts)?, flag_data))
}

/// utility func for ensuring our temporary directory is cleaned up
/// on sigint and sigalrm
pub fn setup_execution_env_cleaner(exec_dir: &Path) -> Result<(), IoError> {
    let exec_dir_path = exec_dir.to_str().unwrap().to_string();

    thread::spawn(move || {
        let mut signals = Signals::new ( &[
            SIGINT,
            SIGALRM,
        ]).unwrap();

        // either signal results in termination
        for _s in &mut signals {
            remove_dir_all(exec_dir_path).expect("removal of execution dir");
            exit(1);
        }
    });

    Ok(())
}

fn apply_landlock(rootfs: &Path) -> Result<(), landlock::RulesetError> {
    let jail = rootfs.to_str().expect("make string out of path");

    let abi = landlock::ABI::V1;
    let status = landlock::Ruleset::new()
        .handle_access(landlock::AccessFs::from_all(abi))?
        .create()?
        .add_rules(landlock::path_beneath_rules(&[jail, "/lib", "/lib64"], landlock::AccessFs::from_read(abi)))?
        .restrict_self()?;

    match status.ruleset {
        landlock::RulesetStatus::FullyEnforced => Ok(()),
        _ => panic!("could not enforce landlock rules"),
    }
}

fn apply_rlimit(as_limit: u64) -> Result<(), IoError> {
    rlimit::setrlimit(rlimit::Resource::AS, as_limit, as_limit)
}

fn apply_seccomp() -> Result<(), Box<dyn Error>> {

    let filter = seccompiler::SeccompFilter::new(
        // everything need for CRT init/load of /bin/bash
        vec![
            (libc::SYS_access, vec![]),
            (libc::SYS_arch_prctl, vec![]),
            (libc::SYS_brk, vec![]),
            (libc::SYS_close, vec![]),
            (libc::SYS_exit_group, vec![]),
            (libc::SYS_fstat, vec![]),
            (libc::SYS_getrandom, vec![]),
            (libc::SYS_mmap, vec![]),
            (libc::SYS_mprotect, vec![]),
            (libc::SYS_munmap, vec![]),
            (libc::SYS_newfstatat, vec![]),
            (libc::SYS_openat, vec![]),
            (libc::SYS_pread64, vec![]),
            (libc::SYS_prlimit64, vec![]),
            (libc::SYS_read, vec![]),
            (libc::SYS_rseq, vec![]),
            (libc::SYS_set_robust_list, vec![]),
            (libc::SYS_set_tid_address, vec![]),
            (libc::SYS_stat, vec![]),
            (libc::SYS_write, vec![]),
            (libc::SYS_memfd_create, vec![]),
            (libc::SYS_execveat, vec![]), // needed for mem exec
            (libc::SYS_getuid, vec![]),
            (libc::SYS_getgid, vec![]),
            (libc::SYS_getpid, vec![]),
            (libc::SYS_rt_sigaction, vec![]),
            (libc::SYS_geteuid, vec![]),
            (libc::SYS_getppid, vec![]),
            (libc::SYS_getcwd, vec![]),
            (libc::SYS_fcntl, vec![]),
            (libc::SYS_getegid, vec![]),
            (libc::SYS_rt_sigprocmask, vec![]),
            (libc::SYS_readlink, vec![]),
            (libc::SYS_sysinfo, vec![]),
            (libc::SYS_getdents64, vec![]),
            (libc::SYS_lseek, vec![]),
            (libc::SYS_dup, vec![]),
            (libc::SYS_socket, vec![]), // AF_UNIX
            (libc::SYS_prctl, vec![]),
            (libc::SYS_ioctl, vec![]),
            (
                libc::SYS_futex,
                vec![
                    seccompiler::SeccompRule::new(vec![
                        seccompiler::SeccompCondition::new(
                            1,
                            seccompiler::SeccompCmpArgLen::Dword,
                            seccompiler::SeccompCmpOp::Eq,
                            129, // futex wake private
                        )?,
                    ])?,
                ]
            ),
        ].into_iter().collect(),
        // mismatch action
        seccompiler::SeccompAction::KillProcess,
        // match action
        seccompiler::SeccompAction::Allow,
        seccompiler::TargetArch::x86_64,
    )?;

    let bpf_prog: seccompiler::BpfProgram = filter.try_into()?;
    seccompiler::apply_filter(&bpf_prog)?;

    let eperm_filter = seccompiler::SeccompFilter::new(
        vec![
            (libc::SYS_socket, vec![]),
            (libc::SYS_prctl, vec![]),
        ].into_iter().collect(),
        // mismatch action
        seccompiler::SeccompAction::Allow,
        // mismatch action
        seccompiler::SeccompAction::Errno(libc::EPERM.try_into().unwrap()),
        seccompiler::TargetArch::x86_64,
    )?;

    let eperm_bpf_prog: seccompiler::BpfProgram = eperm_filter.try_into()?;
    seccompiler::apply_filter(&eperm_bpf_prog)?;

    Ok(())
}

fn apply_sandbox(rootfs: &Path) -> Result<(), Box<dyn Error>> {
    // sandbox does the following
    //  - disallow creation of new files (landlock)
    //  - disallow reading files outside of the tmpdir/lib/lib64 (landlock)
    //  - permit no networking (seccomp)
    //  - permit no ipc (seccomp)
    //  - limit memory allocation size to 20M

    apply_landlock(rootfs)?;
    apply_rlimit(20 * 1024 * 1024)?;
    apply_seccomp()?;

    Ok(())
}

pub fn trace_challenge(challenge_elf: Vec<u8>,
                       args: &[String],
                       tmp_exec_dir: &TempDir,
                       no_sandbox: bool)
         -> Result<Box<dyn Tracer>, Box<dyn Error>> {

    let (stdin_child, stdin_parent) = unistd::pipe()?;
    let (stdout_parent, stdout_child) = unistd::pipe()?;

    let pid = unsafe { libc::fork() };
    match pid {
        -1 => Err(Box::new(
                IoError::new(IoErrorKind::Other, IoError::last_os_error())
                          )),
         0 => {
            unsafe {
                // set up new session to prevent shell from handling SIGSTOP
                libc::setsid();

                // first close parent's pipefds
                libc::close(stdin_parent);
                libc::close(stdout_parent);

                // replace with child's stdin/stdout
                libc::dup2(stdin_child, 0);
                libc::dup2(stdout_child, 1);

                // close stderr
                libc::close(2);

                // close the original fds
                libc::close(stdin_child);
                libc::close(stdout_child);

                if libc::kill(0, libc::SIGSTOP) < 0 {
                    panic!("child failed to sigstop itself");
                }
            }

            // cd to temporary run directory
            set_current_dir(tmp_exec_dir.path()).expect("cd to flag dir");

            // apply sandboxing here
            if !no_sandbox {
                apply_sandbox(tmp_exec_dir.path())
                    .expect("sandbox challenge");
            }

            mem_exec_challenge(challenge_elf, args)
                .expect("failed to mem exec challenge");

            unreachable!("mem exec returned without erroring");
        },
        pid => {
            info!("tracing {}", pid);

            let challenge_stdin = unsafe { File::from_raw_fd(stdin_parent) };
            let challenge_stdout = unsafe { File::from_raw_fd(stdout_parent) };

            setup_execution_env_cleaner(tmp_exec_dir.path())?;

            // perform tracing
            let tracer = sysless_tracer::SyslessTracer::new(
                pid.try_into().unwrap(),
                challenge_stdin,
                challenge_stdout,
            );

            return Ok(Box::new(tracer));
        }
    }
}

fn mem_exec_challenge(challenge_elf: Vec<u8>, args: &[String]) -> IoResult<()> {

    unsafe {
        // the name passed to memfd_create is informational only
        let memfd_name = b"challenge\0";
        let mem_fd = libc::memfd_create(memfd_name.as_ptr() as *const c_char, 0);
        if mem_fd < 0 {
            return Err(IoError::new(IoErrorKind::Other, IoError::last_os_error()));
        }

        let mut mem_file = File::from_raw_fd(mem_fd);
        mem_file.write_all(&challenge_elf[..])?;


        let mut argv = Vec::<*const c_char>::new();
        for arg in args {
            argv.push(arg.as_ptr() as *const c_char);
        }
        argv.push(ptr::null());

        let empty_env: [*const c_char; 1] = [ptr::null()];
        if fexecve(mem_fd, argv.as_ptr(), empty_env.as_ptr()) < 0 {
            return Err(IoError::new(IoErrorKind::Other, IoError::last_os_error()));
        }
    };

    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;
    use nix::unistd;

    fn fork_and_exec(args: &[String],
                     sandbox_dir: Option<PathBuf>,
                     out_sz: usize)
         -> Result<String, Box<dyn Error>> {

        let challenge_elf = load_challenge(&args[0])?;

        let (stdout_parent, stdout_child) = unistd::pipe()?;

        let pid = unsafe { libc::fork() };
        match pid {
            -1 => { panic!("failed to fork child"); }
            0 => {
                unsafe { libc::dup2(stdout_child, 1); }

                if let Some(rootfs) = sandbox_dir {
                    apply_sandbox(rootfs.as_path())?;
                }

                mem_exec_challenge(challenge_elf, &args)?;
                panic!("failed to mem exec");
            },
            _ => {
                let mut wstatus = 0;
                unsafe { libc::wait(&mut wstatus); }

                let mut stdout_reader = unsafe {
                    File::from_raw_fd(stdout_parent)
                };

                // TODO something is getting in the input stream
                // when we run this test with other tests (ie, game_1 test)
                // if you run the test by itself (cargo test mem_exec) then
                // it works if you read out both 'hello' and 'world',
                // but running it with other tests reliably puts a 7f byte
                // after 'hello'
                let mut stdout_buf = vec![0u8; out_sz];
                stdout_reader.read(&mut stdout_buf)?;
                let output = String::from_utf8_lossy(&stdout_buf);

                return Ok(output.to_string());
            }
        }
    }

    #[test]
    fn mem_exec_test() -> Result<(), Box<dyn Error>> {
        let args = [String::from("/bin/echo"),
                    String::from("hello"),
                    String::from("world")];

        let output = fork_and_exec(&args, None, 5)?;

        assert_eq!(output, "hello");

        Ok(())
    }

    #[test]
    #[ignore]
    fn arg_passing_test() -> Result<(), Box<dyn Error>> {
        let args = [String::from("/bin/sh"),
                    String::from("./tests/test_args.sh"),
                    String::from("a"),
                    String::from("z")];

        let challenge_elf = load_challenge(&args[0])?;

        apply_sandbox(&Path::new("./tests/"))?;

        mem_exec_challenge(challenge_elf, &args)
            .expect("expected to exec challenge_elf");
        Ok(())
    }

    #[test]
    fn address_size_sandbox() -> Result<(), Box<dyn Error>> {
        let args_exceeds = [String::from("./tests/allocator"),
                            String::from("20971520")];

        let fail_output = fork_and_exec(&args_exceeds,
                                        Some(PathBuf::from("./tests")),
                                        6)?;

        assert_eq!(fail_output, "Failed");

        let args_behaves = [String::from("./tests/allocator"),
                            String::from("6291456")];

        let output = fork_and_exec(&args_behaves,
                                   Some(PathBuf::from("./tests")),
                                   9)?;

        assert_eq!(output, "Allocated");

        Ok(())
    }
}
