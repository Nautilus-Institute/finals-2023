#![allow(non_upper_case_globals)]
use std::mem::size_of;
use std::error::Error;
use std::path::PathBuf;
use std::collections::HashMap;

use log::{debug, info};

use crate::launcher;
use crate::{next_n, next_u8};
use crate::{calculate_pc_increment};
use crate::interpreter::{Interpreter,
                         InstructionEncoding,
                         GasCost,
                         GameParameters,
                         InterpreterError,
                         InstructionExecResult};

use crate::tracer::{self, Tracer};
use crate::sysless_tracer::SyslessTracer;

const BITFLIP_INSTR_MASK: u8 = 0x80;

pub trait GameTracer: Tracer {
    fn read_byte(self: &mut Self, byte_offset: u64)
         -> Result<u8, tracer::TracerError>;

    fn write_byte(self: &mut Self,
                  byte_offset: u64,
                  value: u8)
         -> Result<(), tracer::TracerError>;
}

use pete::Error as PeteError;

impl From<PeteError> for tracer::TracerError {
    fn from(error: PeteError) -> Self {
        match error {
            PeteError::OS(_err) => {
                tracer::TracerError::InvalidInferiorAddress
            },
            _ => tracer::TracerError::InferiorMemoryAccessError,
        }
    }
}

impl GameTracer for SyslessTracer {

    fn read_byte(&mut self, byte_offset: u64)
         -> Result<u8, tracer::TracerError> {
        let bytes = self.tracee.read_memory(byte_offset, 1)?;

        assert_eq!(bytes.len(), 1, "unexpected number of bytes read");

        Ok(bytes[0])
    }

    fn write_byte(&mut self, byte_offset: u64, value: u8)
         -> Result<(), tracer::TracerError> {

        let write_value: [u8; 1] = [value];
        let wrote = self.tracee.write_memory(byte_offset, &write_value[..])?;

        assert_eq!(wrote, 1, "unexpected number of bytes written");

        Ok(())
    }
}

impl InstructionEncoding {
    // 81 AA - u8, first nibble is reg index, second nibble bit index
    pub const BitFlipMem: Self = InstructionEncoding(BITFLIP_INSTR_MASK | 1);
}

fn get_game_parameters() -> GameParameters {
    let mut catalogue = HashMap::new();      

    catalogue.insert(InstructionEncoding::WriteStdin, GasCost::Variable(10));
    catalogue.insert(InstructionEncoding::ReadStdout, GasCost::Variable(0));
    catalogue.insert(InstructionEncoding::BitFlipMem, GasCost::Fixed(100));

    GameParameters::new(
        100000,
        catalogue,
        GasCost::Fixed(1),
    )
}

fn launch_game_2(interpreter: &mut Interpreter,
                 args: &[String],
                 no_sandbox: bool,
                 artifacts: Vec<PathBuf>,
                 bytecode: &[u8])
     -> Result<(), Box<dyn Error>> {

    interpreter.set_game_parameters(get_game_parameters());

    let (exec_temp_dir, flag_data) = launcher::create_execution_env(artifacts)?;

    interpreter.set_flag(&flag_data);

    let tracer = launcher::trace_challenge(interpreter.get_challenge_elf(),
                                           args,
                                           &exec_temp_dir,
                                           no_sandbox)?;

    interpreter.set_tracer(tracer);

    interpreter.interpret(&bytecode)?;

    Ok(())
}

pub fn play_game_2(challenge_cmd: Vec<String>,
                   no_sandbox: bool,
                   artifacts: Vec<PathBuf>,
                   bytecode: Vec<u8>)
     -> Result<(), Box<dyn Error>> {

    assert_ne!(challenge_cmd.len(), 0);

    let mut challenge_elf = launcher::load_challenge(&challenge_cmd[0])?;

    let mut interpreter = Interpreter::new(&mut challenge_elf, None);

    launch_game_2(&mut interpreter,
                  &challenge_cmd[..],
                  no_sandbox,
                  artifacts,
                  &bytecode)?;

    // output evm final state
    interpreter.dump_registers();
    interpreter.dump_memory();

    println!();
    println!("GAS: {}", interpreter.get_gas());
    println!("SOLVED: {}", interpreter.get_solved());
    println!();

    Ok(())
}

impl<'a> Interpreter<'a> {
    fn exec_bit_flip_memory(&mut self, bytecode: &[u8])
         -> InstructionExecResult {

        let mut bytestream = bytecode;

        let arg = next_u8!(bytestream) as usize;

        let register_idx = (arg >> 4) & 0xf;
        debug!("register index {}", register_idx);

        if register_idx >= self.registers.len() {
            return Err(InterpreterError::InvalidRegisterIndex);
        }

        let offset: usize = self.registers[register_idx] as usize;

        let bit_offset = arg & 0xf;

        if bit_offset >= 8 {
            return Err(InterpreterError::InvalidBitOffset);
        }

        debug!("flipping byte at {offset:x} {bit_offset}");

        if let Some(tracer) = &mut self.tracer {
            let original_byte = match tracer.read_byte(offset as u64) {
                Ok(byte) => byte,
                Err(tracer::TracerError::InvalidInferiorAddress) => {
                    // short-circuit and bail early if the address was invalid
                    // TODO consider setting a register here to indicate status
                    return Ok(());
                },
                Err(err) => return Err(InterpreterError::TracerError(err)),
            };

            debug!("originalbyte [{:x}][<<{}]{:x}",
                        offset,
                        bit_offset,
                        original_byte);

            let flipped_byte = original_byte ^ (1 << bit_offset);

            debug!("flipped byte {:x}", flipped_byte);
            tracer.write_byte(offset as u64, flipped_byte)
                .map_err(|err| InterpreterError::TracerError(err))?;
        }

        self.pc +=
            calculate_pc_increment!(bytestream, bytecode);

        Ok(())
    }

    pub fn interpret(&mut self, bytecode: &[u8])
         -> Result<(), Box<dyn Error>> {
        info!("Interpreting with Game 2 instruction set!");

        while self.pc < bytecode.len() {
            let instruction = InstructionEncoding(bytecode[self.pc]);
            self.pc += 1;

            // charge fixed instructions now
            if self.is_fixed_cost_instruction(instruction.clone()) {
                self.charge_instruction(instruction.clone(), None)?;
            }

            let stream = &bytecode[self.pc..];
            match instruction {
                InstructionEncoding::BitFlipMem => {
                    self.exec_bit_flip_memory(stream)
                }
                instr => self.interpret_instruction(instr, stream)
            }?;
        }

        Ok(())
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::fake_tracer;
    use crate::tracer::TracerError;

    impl GameTracer for fake_tracer::FakeTracer {
        fn write_byte(&mut self,
                      _byte_offset: u64,
                      _value: u8) -> Result<(), TracerError> {
            println!("Fake Tracer write_byte");
            Ok(())
        }

        fn read_byte(&mut self,
                     _byte_offset: u64) -> Result<u8, TracerError> {
            println!("Fake Tracer read_byte");
            Ok(0)
        }
    }

    #[test]
    fn launch_and_read() -> Result<(), Box<dyn Error>> {
        let read_stdout = [0x91, 0x00, 0x00, 0x00, 0x04,
                           0x06, 0x01, 0x00, 0x00];

        let args = [String::from("/bin/ls")];
        let mut challenge_elf = launcher::load_challenge(&args[0])?;
        let mut interpreter = Interpreter::new(&mut challenge_elf, None);

        launch_game_2(&mut interpreter, &args, true, vec![], &read_stdout)?;

        let reg = interpreter.get_registers();
        let mem = interpreter.get_memory();

        assert_ne!(reg[1], 0);

        assert_ne!(mem[0], 0);
        assert_ne!(mem[1], 0);
        assert_ne!(mem[2], 0);
        assert_ne!(mem[3], 0);

        Ok(())
    }

    #[test]
    fn launch_sh() -> Result<(), Box<dyn Error>> {
        let interact = [
                        // mov r1, 0x65
                        0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x65,
                        // mov [0], r1
                        0x02, 0x00, 0x00, 0x01,
                        // mov r1, 0x78
                        0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78,
                        // mov [1], r1
                        0x02, 0x00, 0x01, 0x01,
                        // mov r1, 0x69
                        0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x69,
                        // mov [2], r1
                        0x02, 0x00, 0x02, 0x01,
                        // mov r1, 0x74
                        0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x74,
                        // mov [3], r1
                        0x02, 0x00, 0x03, 0x01,
                        // mov r1, 0x0a
                        0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0a,
                        // mov [4], r1
                        0x02, 0x00, 0x04, 0x01,
                        // write(0, 5)
                        0x90, 0x00, 0x00, 0x00, 0x05];

        let args = [String::from("/bin/sh")];
        let mut challenge_elf = launcher::load_challenge(&args[0])?;
        let mut interpreter = Interpreter::new(&mut challenge_elf, None);

        launch_game_2(&mut interpreter, &args, true, vec![], &interact)?;

        interpreter.dump_memory();

        Ok(())
    }

    #[test]
    fn game2_flip_bit() -> Result<(), Box<dyn Error>> {
        let flip = [
                    // mov r2, 0x401000
                    0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x10, 0x00,
                    // flip_byte [r1, r2], 0x1
                    0x81, 0x21,
                   ];

        let args = [String::from("./tests/flipme")];
        let mut challenge_elf = launcher::load_challenge(&args[0])?;
        let mut interpreter = Interpreter::new(&mut challenge_elf, None);

        launch_game_2(&mut interpreter, &args, false, vec![], &flip)?;

        interpreter.dump_memory();

        Ok(())
    }

    #[test]
    fn io_access() -> Result<(), Box<dyn Error>> {
        let fake_tracer = Box::new(
            fake_tracer::FakeTracer::new(String::from("Hello!"))
        );

        fake_tracer::io_access(fake_tracer)
    }
}
