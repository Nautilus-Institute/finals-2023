#![allow(non_upper_case_globals)]
use std::mem::size_of;
use std::error::Error;
use std::path::PathBuf;
use std::collections::HashMap;

use log::{debug, info};

use crate::launcher;
use crate::{next_n, next_u8, next_u64};
use crate::{calculate_pc_increment};
use crate::interpreter::{Interpreter,
                         InstructionEncoding,
                         GasCost,
                         GameParameters,
                         InterpreterError,
                         InstructionExecResult};

use crate::tracer::{self, Tracer};
use crate::sysless_tracer::SyslessTracer;

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

const BITFLIP_INSTR_MASK: u8 = 0x80;

impl InstructionEncoding {
    // 80 AA AA AA AA AA AA AA AA - u64 offset argument
    pub const BitFlipElf: Self = InstructionEncoding(BITFLIP_INSTR_MASK | 0);
    // 81 AA - u8, first nibble is reg index, second nibble bit index
    pub const BitFlipMem: Self = InstructionEncoding(BITFLIP_INSTR_MASK | 1);
}

fn get_game_parameters() -> GameParameters {
    let mut catalogue = HashMap::new();      

    catalogue.insert(InstructionEncoding::WriteStdin, GasCost::Variable(10));
    catalogue.insert(InstructionEncoding::ReadStdout, GasCost::Variable(0));
    catalogue.insert(InstructionEncoding::BitFlipElf, GasCost::Fixed(100));
    catalogue.insert(InstructionEncoding::BitFlipMem, GasCost::Fixed(100));

    GameParameters::new(
        100000,
        catalogue,
        GasCost::Fixed(1),
    )
}

fn launch_game_3(interpreter: &mut Interpreter,
                 args: &[String],
                 no_sandbox: bool,
                 artifacts: Vec<PathBuf>,
                 bytecode: &[u8])
     -> Result<(), Box<dyn Error>> {

    interpreter.set_game_parameters(get_game_parameters());

    // interpret bit flips until a different instruction occurs
    // mutate bytecode to point to the first non bitfliping instruction
    interpreter.interpret_elf_bit_flips(&bytecode)?;

    let starting_pc = interpreter.get_pc();

    let (exec_temp_dir, flag_data) = launcher::create_execution_env(artifacts)?;

    interpreter.set_flag(&flag_data);

    info!("challenge exec dir {}", exec_temp_dir.path().display());

    let tracer =
        launcher::trace_challenge(interpreter.get_challenge_elf(),
                                  args,
                                  &exec_temp_dir,
                                  no_sandbox)?;

    interpreter.set_tracer(tracer);

    interpreter.interpret(&bytecode[starting_pc..])?;

    Ok(())
}

pub fn play_game_3(challenge_cmd: Vec<String>,
                   no_sandbox: bool,
                   artifacts: Vec<PathBuf>,
                   bytecode: Vec<u8>)
     -> Result<(), Box<dyn Error>> {

    assert_ne!(challenge_cmd.len(), 0);

    let mut challenge_elf = launcher::load_challenge(&challenge_cmd[0])?;

    let mut interpreter = Interpreter::new(&mut challenge_elf, None);

    launch_game_3(&mut interpreter,
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
    fn exec_bit_flip_elf(&mut self, bytecode: &[u8]) -> InstructionExecResult {

        let mut bytestream = bytecode;

        let offset = next_u64!(bytestream);

        let byte_offset = (offset / 8) as usize;
        let bit_offset = offset % 8;

        if byte_offset > self.challenge_elf.len() {
            return Err(InterpreterError::InvalidBitOffset);
        }

        let original_byte = self.challenge_elf[byte_offset];
        debug!("originalbyte [{:x}][<<{}]{:x}",
                    byte_offset,
                    bit_offset,
                    original_byte);

        let flipped_byte = original_byte ^ (1 << bit_offset);

        debug!("flipped bit {:x}", flipped_byte);
        self.challenge_elf[byte_offset] = flipped_byte;

        self.pc +=
            calculate_pc_increment!(bytestream, bytecode);

        Ok(())
    }

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

    pub fn get_pc(&self) -> usize {
        self.pc
    }

    pub fn set_pc(&mut self, pc: usize) {
        self.pc = pc;
    }

    pub fn interpret_elf_bit_flips(&mut self, bytecode: &[u8])
         -> Result<(), Box<dyn Error>> {
        while self.pc < bytecode.len() {
            let instruction = InstructionEncoding(bytecode[self.pc]);

            if let InstructionEncoding::BitFlipElf = instruction {

                self.pc += 1;

                self.charge_instruction(instruction.clone(), None)?;

                let stream = &bytecode[self.pc..];
                self.exec_bit_flip_elf(stream)?

            } else {
                break;
            }
        }

        Ok(())
    }

    pub fn interpret(&mut self, bytecode: &[u8])
         -> Result<(), Box<dyn Error>> {
        info!("Interpreting with Game 3 instruction set!");

        self.pc = 0;

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
    use crate::fake_tracer::FakeTracer;
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

        launch_game_3(&mut interpreter, &args, true, vec![], &read_stdout)?;

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
                        0x00, 0x01, 0x00, 0x00, 0x00, 0x65,
                        // mov [0], r1
                        0x02, 0x00, 0x00, 0x01,
                        // mov r1, 0x78
                        0x00, 0x01, 0x00, 0x00, 0x00, 0x78,
                        // mov [1], r1
                        0x02, 0x00, 0x01, 0x01,
                        // mov r1, 0x69
                        0x00, 0x01, 0x00, 0x00, 0x00, 0x69,
                        // mov [2], r1
                        0x02, 0x00, 0x02, 0x01,
                        // mov r1, 0x74
                        0x00, 0x01, 0x00, 0x00, 0x00, 0x74,
                        // mov [3], r1
                        0x02, 0x00, 0x03, 0x01,
                        // mov r1, 0x0a
                        0x00, 0x01, 0x00, 0x00, 0x00, 0x0a,
                        // mov [4], r1
                        0x02, 0x00, 0x04, 0x01,
                        // write(0, 5)
                        0x90, 0x00, 0x00, 0x00, 0x05];

        let args = [String::from("/bin/sh")];
        let mut challenge_elf = launcher::load_challenge(&args[0])?;
        let mut interpreter = Interpreter::new(&mut challenge_elf, None);

        launch_game_3(&mut interpreter, &args, true, vec![], &interact)?;

        interpreter.dump_memory();

        Ok(())
    }

    #[test]
    fn io_access() -> Result<(), Box<dyn Error>> {
        let fake_tracer = Box::new(FakeTracer::new(String::from("Hello!")));
        fake_tracer::io_access(fake_tracer)
    }

    #[test]
    fn io_var_access() -> Result<(), Box<dyn Error>> {
        let fake_tracer = Box::new(FakeTracer::new(String::from("Hello!")));
        fake_tracer::io_var_access(fake_tracer)
    }

    fn get_fake_game_parameters(budget: usize) -> GameParameters {
        let mut catalogue = HashMap::new();

        catalogue.insert(InstructionEncoding::WriteStdin,
                         GasCost::Variable(10));

        catalogue.insert(InstructionEncoding::ReadStdout,
                         GasCost::Variable(10));

        catalogue.insert(InstructionEncoding::BitFlipElf,
                         GasCost::Fixed(50));

        GameParameters::new(
            budget,
            catalogue,
            GasCost::Fixed(10),
        )
    }

    #[test]
    fn elf_bit_flipping() -> Result<(), Box<dyn Error>> {
        let mut fake_elf = [0xff, 0x00, 0b01010101];

        let mut interp = Interpreter::new(&mut fake_elf, None);

        let flp_moves = [
                         // flip 0
                         0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                         // flip 7
                         0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07,
                         // flip 17
                         0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11,
                         // flip 19
                         0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x13,
                         // flip 21
                         0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x15,
                         // flip 23
                         0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x17];

        interp.interpret_elf_bit_flips(&flp_moves)?;

        assert_eq!(fake_elf[0], 0x7e);
        assert_eq!(fake_elf[2], 0b11111111);

        Ok(())
    }

    #[test]
    fn test_budget_exhaustion() -> Result<(), Box<dyn Error>> {
        let mut fake_elf = [0, 0, 0, 0];

        let mut interp = Interpreter::new(&mut fake_elf, None);

        interp.set_game_parameters(get_fake_game_parameters(80));

        // only one instruction should be executed
        let budget_moves = [
                         0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                         // flip 7
                         0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07
                         ];


        let result = interp.interpret_elf_bit_flips(&budget_moves);

        assert!(result.is_err());

        assert_eq!(fake_elf[0], 1);

        Ok(())
    }

    #[test]
    fn io_budget() -> Result<(), Box<dyn Error>> {
        let mut fake_elf = [0x41, 0x42, 0x43, 44];

        let fake_tracer = Box::new(FakeTracer::new(String::from("Hello!")));
        let mut interp = Interpreter::new(&mut fake_elf, Some(fake_tracer));

        interp.set_game_parameters(get_fake_game_parameters(80));

        let io_moves = [
                         0x91, 0x00, 0x00, 0x00, 0x04,
                         0x91, 0x01, 0x00, 0x00, 0x04,
                         0x00, 0x01, 0x20, 0x4e, 0x61, 0x68,
                         0x04, 0x00, 0x05, 0x01,
                         0x90, 0x00, 0x00, 0x00, 0x08];

        let result = interp.interpret(&io_moves);

        assert!(result.is_err());

        let mem = interp.get_memory();

        let region1 = mem[0..4].to_vec();
        let string_val1 = String::from_utf8_lossy(&region1);

        assert_eq!(string_val1, String::from("Hell"));

        let region2 = mem[0x100..0x102].to_vec();
        let string_val2 = String::from_utf8_lossy(&region2);

        assert_eq!(string_val2, String::from("o!"));

        // ensure the above succeeded

        let regs = interp.get_registers();

        // ensure that 0x01 was not set as the instruction exceeded budget
        assert_eq!(regs[1], 0);

        Ok(())
    }

    #[test]
    fn io_stdout_var_budget() -> Result<(), Box<dyn Error>> {
        let mut fake_elf = [0x41, 0x42, 0x43, 44];

        let fake_tracer = Box::new(FakeTracer::new(String::from("Hello!")));
        let mut interp = Interpreter::new(&mut fake_elf, Some(fake_tracer));

        // budget is 80
        interp.set_game_parameters(get_fake_game_parameters(80));

        let io_var_moves = [
                         0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06,
                         0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
                         0x93, 0x00, 0x01,
                         0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x20, 0x4e, 0x61, 0x68,
                         0x04, 0x00, 0x05, 0x03,
                         0x90, 0x00, 0x00, 0x00, 0x08];

        let result = interp.interpret(&io_var_moves);

        assert!(result.is_err());

        let mem = interp.get_memory();

        let region1 = mem[0..4].to_vec();
        let string_val1 = String::from_utf8_lossy(&region1);

        assert_eq!(string_val1, String::from("Hell"));

        // ensure the above succeeded

        let regs = interp.get_registers();

        // ensure that 'Nah' was not set as the instruction exceeded budget
        assert_eq!(regs[3], 0);

        Ok(())
    }

    #[test]
    fn io_stdin_var_budget() -> Result<(), Box<dyn Error>> {
        let mut fake_elf = [0x41, 0x42, 0x43, 44];

        let fake_tracer = Box::new(FakeTracer::new(String::from("Hello!")));
        let mut interp = Interpreter::new(&mut fake_elf, Some(fake_tracer));

        // budget is 80
        interp.set_game_parameters(get_fake_game_parameters(80));

        let io_var_moves = [
                         0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06,
                         0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
                         0x92, 0x00, 0x01,
                         0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x20, 0x4e, 0x61, 0x68,
                         0x04, 0x00, 0x05, 0x03,
                         0x90, 0x00, 0x00, 0x00, 0x08];

        let result = interp.interpret(&io_var_moves);

        assert!(result.is_err());

        let regs = interp.get_registers();

        // ensure that 'Nah' was not set as the instruction exceeded budget
        assert_eq!(regs[3], 0);

        Ok(())
    }
}
