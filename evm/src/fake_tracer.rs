use std::cmp;
use std::error::Error;

use crate::interpreter::Interpreter;
use crate::tracer::{Tracer, TracerError};

#[cfg(feature = "game_1")]
use crate::game_1::GameTracer;
#[cfg(feature = "game_2")]
use crate::game_2::GameTracer;
#[cfg(feature = "game_3")]
use crate::game_3::GameTracer;

pub struct FakeTracer {
    stdin: Vec<u8>,
    stdout: Vec<u8>,
    stdout_pos: usize,
}

impl FakeTracer {
    pub fn new(stdout: String) -> Self {
        Self {
            stdin: Vec::<u8>::new(),
            stdout: stdout.into_bytes(),
            stdout_pos: 0,
        }
    }

    pub fn copy_stdin(&self) -> Vec<u8> {
        self.stdin.clone()
    }
}

impl Tracer for FakeTracer {
    fn write_out(&mut self, buf: &[u8]) -> Result<usize, TracerError> {
        self.stdin.extend_from_slice(buf);

        Ok(buf.len())
    }

    fn read_in(&mut self, buf: &mut [u8]) -> Result<usize, TracerError> {
        let read_n = cmp::min(buf.len(),
                              self.stdout.len() - self.stdout_pos);

        let limit = self.stdout_pos+read_n;
        let content = &self.stdout[self.stdout_pos..limit];

        let dest = &mut buf[..read_n];

        dest.copy_from_slice(content);
        self.stdout_pos += read_n;

        Ok(read_n)
    }
}

pub fn io_access(tracer: Box<dyn GameTracer>) -> Result<(), Box<dyn Error>> {
    let mut fake_elf = [0x41, 0x42, 0x43, 44];

    let mut interp = Interpreter::new(&mut fake_elf, Some(tracer));

    let io_moves = [
                     0x91, 0x00, 0x00, 0x00, 0x04,
                     0x91, 0x01, 0x00, 0x00, 0x04,
                     0x00, 0x01, 0x20, 0x4e, 0x61, 0x68,
                     0x04, 0x00, 0x05, 0x01,
                     0x90, 0x00, 0x00, 0x00, 0x08];

    interp.interpret(&io_moves)?;

    let mem = interp.get_memory();

    let region1 = mem[0..4].to_vec();
    let string_val1 = String::from_utf8_lossy(&region1);

    assert_eq!(string_val1, String::from("Hell"));

    let region2 = mem[0x100..0x102].to_vec();
    let string_val2 = String::from_utf8_lossy(&region2);

    assert_eq!(string_val2, String::from("o!"));

    Ok(())
}

pub fn io_var_access(tracer: Box<dyn GameTracer>)
     -> Result<(), Box<dyn Error>> {
    let mut fake_elf = [0x41, 0x42, 0x43, 0x44];

    let mut interp = Interpreter::new(&mut fake_elf, Some(tracer));

    let io_var_moves = [
                        0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04,
                        0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
                        0x93, 0x00, 0x01,
                        0x93, 0x02, 0x01,
                        0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x20, 0x4e, 0x61, 0x68,
                        0x04, 0x00, 0x05, 0x03,
                        0x0D, 0x01, 0x01,
                        0x92, 0x00, 0x01];

    interp.interpret(&io_var_moves)?;

    let mem = interp.get_memory();

    let region1 = mem[0..4].to_vec();
    let string_val1 = String::from_utf8_lossy(&region1);

    assert_eq!(string_val1, String::from("Hell"));

    let region2 = mem[0x100..0x102].to_vec();
    let string_val2 = String::from_utf8_lossy(&region2);

    assert_eq!(string_val2, String::from("o!"));

    Ok(())
}
