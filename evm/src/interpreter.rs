#![allow(non_upper_case_globals)]
use std::mem::size_of;
use std::collections::HashMap;

use log::{debug, error};
use thiserror::Error;

use crate::FLAG_LEN;
use crate::tracer;

#[cfg(feature = "game_1")]
use crate::game_1::GameTracer as Tracer;
#[cfg(feature = "game_2")]
use crate::game_2::GameTracer as Tracer;
#[cfg(feature = "game_3")]
use crate::game_3::GameTracer as Tracer;

#[macro_export]
macro_rules! next_n {
    ($slice:ident, $ty:ty, $amount:expr) => {
        if $slice.len() < $amount {
            return Err(InterpreterError::IncompleteInstruction);
        } else {
            let __val = <$ty>::from_be_bytes($slice[..$amount].try_into()
                .map_err(|_err| InterpreterError::IncompleteInstruction)?);
            $slice = &$slice[$amount..];
            __val
        }
    }
}

#[macro_export]
macro_rules! next_u8 {
    ($slice:ident) => {
        next_n!($slice, u8, size_of::<u8>())
    }
}

#[macro_export]
macro_rules! next_u16 {
    ($slice:ident) => {
        next_n!($slice, u16, size_of::<u16>())
    }
}

#[macro_export]
macro_rules! next_u32 {
    ($slice:ident) => {
        next_n!($slice, u32, size_of::<u32>())
    }
}

#[macro_export]
macro_rules! next_u64 {
    ($slice:ident) => {
        next_n!($slice, u64, size_of::<u64>())
    }
}

#[macro_export]
macro_rules! calculate_pc_increment {
    ($stream:ident, $original:ident) => {
        ($stream.as_ptr() as usize) - ($original.as_ptr() as usize)
    }
}

const EVM_REGISTER_COUNT: usize = 12;
const EVM_MEMORY_SIZE: usize  = 1024;
const EVM_MAX_BYTECODE_LEN: usize = 0xFFFF;

const JUMP_INSTR_MASK: u8 = 0x10;
const IO_INSTR_MASK: u8 = 0x90;
const FLAG_INSTR_MASK: u8 = 0xF0;

pub enum GasCost {
    Fixed(usize),
    Variable(usize),
    //Forbidden,
}

pub type InstructionExecResult = Result<(), InterpreterError>;

#[derive(Clone, Eq, PartialEq, Hash)]
pub struct InstructionEncoding(pub u8);

impl InstructionEncoding {
    // 00 AA BB BB BB BB BB BB BB BB - u8 register index, u64 immediate
    pub const StoreReg: Self   = InstructionEncoding(0);
    // 01 AA BB - two u8 register indexes
    pub const MoveReg: Self    = InstructionEncoding(1);
    // 02 AA AA BB - u16 address, u8 register index
    pub const StoreMem8: Self  = InstructionEncoding(2);
    // 03 AA AA BB - u16 address, u8 register index
    pub const StoreMem16: Self = InstructionEncoding(3);
    // 04 AA AA BB - u16 address, u8 register index
    pub const StoreMem32: Self = InstructionEncoding(4);
    // 05 AA BB BB - u8 register index, u16 address
    pub const StoreMem64: Self = InstructionEncoding(5);

    // 06 AA BB BB - u8 register index, u16 address
    pub const LoadMem: Self    = InstructionEncoding(6);

    // 07 AA BB - u8 register index, u8 register index
    pub const StoreReg8: Self = InstructionEncoding(7);
    // 08 AA BB - u8 register index, u8 register index
    pub const StoreReg16: Self = InstructionEncoding(8);
    // 09 AA BB - u8 register index, u8 register index
    pub const StoreReg32: Self = InstructionEncoding(9);
    // 0A AA BB - u8 register index, u8 register index
    pub const StoreReg64: Self = InstructionEncoding(10);

    // 0B AA BB - u8 register index, u8 register index
    pub const LoadReg: Self = InstructionEncoding(11);

    // 0C AA BB - u8 register index,  u8 register index
    pub const AddReg: Self     = InstructionEncoding(12);
    // 0D AA BB - u8 register index,  u8 register index
    pub const SubReg: Self     = InstructionEncoding(13);
    // 0E AA BB - u8 register index,  u8 register index
    pub const AndReg: Self     = InstructionEncoding(14);
    // 0F AA BB - u8 register index,  u8 register_idx
    pub const LshiftReg: Self  = InstructionEncoding(15);

    // 10 AA AA - u16 address
    pub const Jump: Self = InstructionEncoding(JUMP_INSTR_MASK | 0);
    // 11 AA BB CC CC - u8 register index, u8 register index, u16
    pub const JumpIfEq: Self = InstructionEncoding(JUMP_INSTR_MASK | 1);
    // 12 AA BB CC CC - u8 register index, u8 register index, u16
    pub const JumpIfNeq: Self = InstructionEncoding(JUMP_INSTR_MASK | 2);
    // 13 AA BB CC CC - u8 register index, u8 register index, u16
    pub const JumpIfLt: Self = InstructionEncoding(JUMP_INSTR_MASK | 3);
    // 14 AA BB CC CC - u8 register index, u8 register index, u16
    pub const JumpIfGt: Self = InstructionEncoding(JUMP_INSTR_MASK | 4);

    // 90 AA AA BB BB - u16 src address, u16 len to write
    pub const WriteStdin: Self = InstructionEncoding(IO_INSTR_MASK | 0);
    // 91 AA AA BB BB - u16 dst address, u16 len to read
    pub const ReadStdout: Self = InstructionEncoding(IO_INSTR_MASK | 1);
    // 92 AA BB - u8 register index, u8 register index
    pub const WriteStdinReg: Self = InstructionEncoding(IO_INSTR_MASK | 2);
    // 93 AA BB - u8 register index, u8 register index
    pub const ReadStdoutReg: Self = InstructionEncoding(IO_INSTR_MASK | 3);

    // F0 AA AA - u16 src address
    pub const SubmitFlag: Self = InstructionEncoding(FLAG_INSTR_MASK | 0);
}

pub struct GameParameters {
    budget: usize,
    instruction_catalogue: HashMap<InstructionEncoding, GasCost>,
    default_cost: GasCost,
}

impl GameParameters {
    pub fn new(budget: usize,
               catalogue: HashMap<InstructionEncoding, GasCost>,
               default_cost: GasCost) -> Self {
        Self {
            budget: budget,
            instruction_catalogue: catalogue,
            default_cost: default_cost
        }
    }
}

#[derive(Debug, Error)]
pub enum InterpreterError {
    #[error("Invalid instruction")]
    InvalidInstruction,
    #[error("Instruction encoding was not complete")]
    IncompleteInstruction,
    #[error("Invalid register index")]
    InvalidRegisterIndex,
    #[error("Invalid memory address")]
    InvalidMemoryAddress,
    #[error("Invalid memory range")]
    InvalidMemoryRange,
    #[error("Bit offset exceeds size of challenge")]
    InvalidBitOffset,
    #[error("Budget total was exceeded")]
    BudgetExceeded,
    #[error("Tracer encountered error {0}")]
    TracerError(tracer::TracerError),
}

pub struct Interpreter<'a> {
    pub challenge_elf: &'a mut [u8],
    pub pc: usize,
    pub tracer: Option<Box<dyn Tracer>>,
    pub registers: [u64; EVM_REGISTER_COUNT],
    pub memory: [u8; EVM_MEMORY_SIZE],
    game_params: Option<GameParameters>,
    gas: usize,
    solved: bool,
    flag: Option<[u8; FLAG_LEN]>,
}

impl<'a> Interpreter<'a> {
    pub fn new(challenge_elf: &'a mut [u8],
               tracer: Option<Box<dyn Tracer>>)
         -> Self {
        Self {
            challenge_elf: challenge_elf,
            pc: 0,
            registers: [0_u64; 12],
            memory: [0_u8; 1024],
            game_params: None,
            gas: usize::MAX,
            solved: false,
            flag: None,
            tracer: tracer,
        }
    }

    fn exec_store_reg(&mut self, bytecode: &[u8]) -> InstructionExecResult {

        let mut bytestream = bytecode;

        let register_idx = next_u8!(bytestream) as usize;

        if register_idx >= self.registers.len() {
            return Err(InterpreterError::InvalidRegisterIndex);
        }

        let value = next_u64!(bytestream);

        self.registers[register_idx] = value;
        debug!("regs[{}] <- {}", register_idx, value);

        self.pc +=
            calculate_pc_increment!(bytestream, bytecode);

        Ok(())
    }

    fn exec_move_reg(&mut self, bytecode: &[u8]) -> InstructionExecResult {

        let mut bytestream = bytecode;

        let dst_register_idx = next_u8!(bytestream) as usize;

        if dst_register_idx >= self.registers.len() {
            return Err(InterpreterError::InvalidRegisterIndex);
        }

        let src_register_idx = next_u8!(bytestream) as usize;

        if src_register_idx >= self.registers.len() {
            return Err(InterpreterError::InvalidRegisterIndex);
        }

        self.registers[dst_register_idx] = self.registers[src_register_idx];
        debug!("regs[{}] <- regs[{}]", dst_register_idx, src_register_idx);

        self.pc +=
            calculate_pc_increment!(bytestream, bytecode);

        Ok(())
    }

    fn store_mem_inner(&mut self, write_size: usize, bytecode: &[u8])
         -> InstructionExecResult {

        let mut bytestream = bytecode;

        let address = next_u16!(bytestream) as usize;

        // ensure enough space to complete the write
        if (address + write_size) > self.memory.len() {
            return Err(InterpreterError::InvalidMemoryAddress);
        }

        let register_idx = next_u8!(bytestream) as usize;

        if register_idx >= self.registers.len() {
            return Err(InterpreterError::InvalidRegisterIndex);
        }

        let destination = &mut self.memory[address..address+write_size];
        // TODO: find a better way to do this with generics, trouble is
        // I can't find a good bound for T that guarantees a to_le_bytes
        // function.. best idea is to define my own trait, but that's as
        // much hassle as a dumb match statement
        match write_size {
            1 => {
                destination.copy_from_slice(
                    &(self.registers[register_idx] as u8).to_le_bytes()
                );
            }
            2 => {
                destination.copy_from_slice(
                    &(self.registers[register_idx] as u16).to_le_bytes()
                );
            }
            4 => {
                destination.copy_from_slice(
                    &(self.registers[register_idx] as u32).to_le_bytes()
                );
            }
            8 => {
                destination.copy_from_slice(
                    &(self.registers[register_idx] as u64).to_le_bytes()
                );
            }
            _ => unreachable!(),
        }

        debug!("memory[{:x}] <- regs[{}]", address, register_idx);

        self.pc +=
            calculate_pc_increment!(bytestream, bytecode);

        Ok(())
    }

    fn exec_store_mem8(&mut self, bytecode: &[u8]) -> InstructionExecResult {
        self.store_mem_inner(1, bytecode)
    }

    fn exec_store_mem16(&mut self, bytecode: &[u8]) -> InstructionExecResult {
        self.store_mem_inner(2, bytecode)
    }

    fn exec_store_mem32(&mut self, bytecode: &[u8]) -> InstructionExecResult {
        self.store_mem_inner(4, bytecode)
    }

    fn exec_store_mem64(&mut self, bytecode: &[u8]) -> InstructionExecResult {
        self.store_mem_inner(8, bytecode)
    }

    fn exec_load_mem(&mut self, bytecode: &[u8]) -> InstructionExecResult {

        let mut bytestream = bytecode;

        let register_idx = next_u8!(bytestream) as usize;

        if register_idx >= self.registers.len() {
            return Err(InterpreterError::InvalidRegisterIndex);
        }

        let address = next_u16!(bytestream) as usize;

        let read_sz = size_of::<u64>();
        if (address + read_sz) > self.memory.len() {
            return Err(InterpreterError::InvalidMemoryAddress);
        }

        debug!("regs[{}] <- memory[{}]", register_idx, address);

        self.registers[register_idx] =
            u64::from_le_bytes(self.memory[address..address+read_sz]
             .try_into()
             .map_err(|_err| InterpreterError::InvalidMemoryAddress)?);

        debug!("regs[{}]: {:x}", register_idx, self.registers[register_idx]);

        self.pc +=
            calculate_pc_increment!(bytestream, bytecode);

        Ok(())
    }

    fn store_reg_inner(&mut self, write_size: usize, bytecode: &[u8])
         -> InstructionExecResult {

        let mut bytestream = bytecode;

        let register_idx_addr = next_u8!(bytestream) as usize;

        if register_idx_addr >= self.registers.len() {
            return Err(InterpreterError::InvalidRegisterIndex);
        }

        let address = self.registers[register_idx_addr] as usize;

        if (address + write_size) > self.memory.len() {
            return Err(InterpreterError::InvalidMemoryAddress);
        }

        let register_idx = next_u8!(bytestream) as usize;

        if register_idx >= self.registers.len() {
            return Err(InterpreterError::InvalidRegisterIndex);
        }

        let destination = &mut self.memory[address..address+write_size];
        match write_size {
            1 => {
                destination.copy_from_slice(
                    &(self.registers[register_idx] as u8).to_le_bytes()
                );
            }
            2 => {
                destination.copy_from_slice(
                    &(self.registers[register_idx] as u16).to_le_bytes()
                )
            }
            4 => {
                destination.copy_from_slice(
                    &(self.registers[register_idx] as u32).to_le_bytes()
                )
            }
            8 => {
                destination.copy_from_slice(
                    &(self.registers[register_idx] as u64).to_le_bytes()
                )
            }
            _ => unreachable!(),
        }

        debug!("memory[{:x}] <- regs[{}]", address, register_idx);

        self.pc +=
            calculate_pc_increment!(bytestream, bytecode);

        Ok(())
    }

    fn exec_store_reg8(&mut self, bytecode: &[u8]) -> InstructionExecResult {
        self.store_reg_inner(1, bytecode)
    }

    fn exec_store_reg16(&mut self, bytecode: &[u8]) -> InstructionExecResult {
        self.store_reg_inner(2, bytecode)
    }

    fn exec_store_reg32(&mut self, bytecode: &[u8]) -> InstructionExecResult {
        self.store_reg_inner(4, bytecode)
    }

    fn exec_store_reg64(&mut self, bytecode: &[u8]) -> InstructionExecResult {
        self.store_reg_inner(8, bytecode)
    }

    fn exec_load_reg(&mut self, bytecode: &[u8]) -> InstructionExecResult {

        let mut bytestream = bytecode;

        let register_idx = next_u8!(bytestream) as usize;

        if register_idx >= self.registers.len() {
            return Err(InterpreterError::InvalidRegisterIndex);
        }

        let register_idx_src = next_u8!(bytestream) as usize;

        if register_idx_src >= self.registers.len() {
            return Err(InterpreterError::InvalidRegisterIndex);
        }

        let address = self.registers[register_idx_src] as usize;

        let read_sz = size_of::<u64>();
        if (address + read_sz) > self.memory.len() {
            return Err(InterpreterError::InvalidMemoryAddress);
        }

        debug!("regs[{}] <- memory[{}]", register_idx, address);

        self.registers[register_idx] =
            u64::from_le_bytes(self.memory[address..address+read_sz]
              .try_into()
              .map_err(|_err| InterpreterError::InvalidMemoryAddress)?);

        debug!("regs[{}]: {:x}", register_idx, self.registers[register_idx]);

        self.pc +=
            calculate_pc_increment!(bytestream, bytecode);

        Ok(())
    }

    fn exec_jump(&mut self, bytecode: &[u8]) -> InstructionExecResult {

        let mut bytestream = bytecode;

        let new_pc = next_u16!(bytestream) as usize;

        self.pc = new_pc;

        let _ = bytestream;

        Ok(())
    }

    fn jump_if_inner(&mut self,
                        cmp: impl Fn(u64, u64) -> bool,
                        bytecode: &[u8])
         -> InstructionExecResult {

        let mut bytestream = bytecode;

        let register_idx1 = next_u8!(bytestream) as usize;
        let register_idx2 = next_u8!(bytestream) as usize;

        if register_idx1 >= self.registers.len() {
            return Err(InterpreterError::InvalidRegisterIndex);
        }

        if register_idx2 >= self.registers.len() {
            return Err(InterpreterError::InvalidRegisterIndex);
        }

        let new_pc = next_u16!(bytestream) as usize;

        let value1 = self.registers[register_idx1];
        let value2 = self.registers[register_idx2];

        debug!("jump to {:x}", new_pc);

        if cmp(value1, value2) {
            self.pc = new_pc;
        } else {
            self.pc += calculate_pc_increment!(bytestream, bytecode);
        }

        Ok(())
    }

    fn exec_jump_if_eq(&mut self, bytecode: &[u8]) -> InstructionExecResult {
        self.jump_if_inner(|r1, r2| r1 == r2, bytecode)
    }

    fn exec_jump_if_neq(&mut self, bytecode: &[u8]) -> InstructionExecResult {
        self.jump_if_inner(|r1, r2| r1 != r2, bytecode)
    }

    fn exec_jump_if_lt(&mut self, bytecode: &[u8]) -> InstructionExecResult {
        self.jump_if_inner(|r1, r2| r1 < r2, bytecode)
    }

    fn exec_jump_if_gt(&mut self, bytecode: &[u8]) -> InstructionExecResult {
        self.jump_if_inner(|r1, r2| r1 > r2, bytecode)
    }

    fn exec_add_reg(&mut self, bytecode: &[u8]) -> InstructionExecResult {

        let mut bytestream = bytecode;

        let register_idx1 = next_u8!(bytestream) as usize;
        let register_idx2 = next_u8!(bytestream) as usize;

        if register_idx1 >= self.registers.len() {
            return Err(InterpreterError::InvalidRegisterIndex);
        }

        if register_idx2 >= self.registers.len() {
            return Err(InterpreterError::InvalidRegisterIndex);
        }

        debug!("regs[{}] += regs[{}]", register_idx1, register_idx2);

        self.registers[register_idx1] += self.registers[register_idx2];

        self.pc += calculate_pc_increment!(bytestream, bytecode);

        Ok(())
    }

    fn exec_sub_reg(&mut self, bytecode: &[u8]) -> InstructionExecResult {

        let mut bytestream = bytecode;

        let register_idx1 = next_u8!(bytestream) as usize;
        let register_idx2 = next_u8!(bytestream) as usize;

        if register_idx1 >= self.registers.len() {
            return Err(InterpreterError::InvalidRegisterIndex);
        }

        if register_idx2 >= self.registers.len() {
            return Err(InterpreterError::InvalidRegisterIndex);
        }

        self.registers[register_idx1] -= self.registers[register_idx2];

        self.pc += calculate_pc_increment!(bytestream, bytecode);

        Ok(())
    }

    fn exec_and_reg(&mut self, bytecode: &[u8]) -> InstructionExecResult {

        let mut bytestream = bytecode;

        let register_idx1 = next_u8!(bytestream) as usize;
        let register_idx2 = next_u8!(bytestream) as usize;

        if register_idx1 >= self.registers.len() {
            return Err(InterpreterError::InvalidRegisterIndex);
        }

        if register_idx2 >= self.registers.len() {
            return Err(InterpreterError::InvalidRegisterIndex);
        }

        self.registers[register_idx1] &= self.registers[register_idx2];

        debug!("regs[{}]: {:x}", register_idx1, self.registers[register_idx1]);

        self.pc += calculate_pc_increment!(bytestream, bytecode);

        Ok(())
    }

    fn exec_lshift_reg(&mut self, bytecode: &[u8]) -> InstructionExecResult {

        let mut bytestream = bytecode;

        let register_idx1 = next_u8!(bytestream) as usize;

        if register_idx1 >= self.registers.len() {
            return Err(InterpreterError::InvalidRegisterIndex);
        }

        let register_idx2 = next_u8!(bytestream) as usize;

        if register_idx2 >= self.registers.len() {
            return Err(InterpreterError::InvalidRegisterIndex);
        }

        self.registers[register_idx1] <<= self.registers[register_idx2];

        debug!("regs[{}]: {:x}", register_idx1, self.registers[register_idx1]);

        self.pc += calculate_pc_increment!(bytestream, bytecode);

        Ok(())
    }

    fn exec_write_stdin(&mut self, bytecode: &[u8]) -> InstructionExecResult {

        let mut bytestream = bytecode;

        let src = next_u16!(bytestream) as usize;
        let n = next_u16!(bytestream) as usize;

        if src + n > self.memory.len() {
            return Err(InterpreterError::InvalidMemoryRange);
        }

        self.charge_instruction(InstructionEncoding::WriteStdin, Some(n))?;

        debug!("writing stdin from mem[{:x}+{}]", src, n);

        let _n_bytes = if let Some(tracer) = &mut self.tracer {
            tracer.write_out(&self.memory[src..src+n])
                .map_err(|err| InterpreterError::TracerError(err))?
        } else {
            error!("No tracer set in write_stdin");
            0
        };

        self.pc +=
            calculate_pc_increment!(bytestream, bytecode);

        Ok(())
    }

    fn exec_read_stdout(&mut self, bytecode: &[u8]) -> InstructionExecResult {

        let mut bytestream = bytecode;

        let dst = next_u16!(bytestream) as usize;
        let n = next_u16!(bytestream) as usize;

        if dst + n > self.memory.len() {
            return Err(InterpreterError::InvalidMemoryRange);
        }

        self.charge_instruction(InstructionEncoding::ReadStdout, Some(n))?;

        debug!("reading stdout into mem[{:x}+{}]", dst, n);

        let _n_bytes = if let Some(tracer) = &mut self.tracer {
            tracer.read_in(&mut self.memory[dst..dst+n])
                .map_err(|err| InterpreterError::TracerError(err))?
        } else {
            error!("No tracer set in read_stdout");
            0
        };

        self.pc +=
            calculate_pc_increment!(bytestream, bytecode);

        // adjust cost based on what happened
        Ok(())
    }

    fn exec_write_stdin_reg(&mut self, bytecode: &[u8])
         -> InstructionExecResult {

        let mut bytestream = bytecode;

        let register_idx_src = next_u8!(bytestream) as usize;
        if register_idx_src >= self.registers.len() {
            return Err(InterpreterError::InvalidRegisterIndex);
        }

        let register_idx_len = next_u8!(bytestream) as usize;
        if register_idx_len >= self.registers.len() {
            return Err(InterpreterError::InvalidRegisterIndex);
        }

        let src = self.registers[register_idx_src] as usize;
        let n = self.registers[register_idx_len] as usize;

        if src >= self.memory.len() {
            return Err(InterpreterError::InvalidMemoryRange);
        }

        if src + n > self.memory.len() {
            return Err(InterpreterError::InvalidMemoryRange);
        }

        // charge same as stdin
        self.charge_instruction(InstructionEncoding::WriteStdin, Some(n))?;

        debug!("writing stdin from mem[{:x}+{}]", src, n);

        let _n_bytes = if let Some(tracer) = &mut self.tracer {
            tracer.write_out(&self.memory[src..src+n])
                .map_err(|err| InterpreterError::TracerError(err))?
        } else {
            error!("No tracer set in write_stdin_reg");
            0
        };

        self.pc +=
            calculate_pc_increment!(bytestream, bytecode);

        // adjust cost based on what happened
        Ok(())
    }

    fn exec_read_stdout_reg(&mut self, bytecode: &[u8])
         -> InstructionExecResult {

        let mut bytestream = bytecode;

        let register_idx_dst = next_u8!(bytestream) as usize;
        if register_idx_dst >= self.registers.len() {
            return Err(InterpreterError::InvalidRegisterIndex);
        }

        let register_idx_len = next_u8!(bytestream) as usize;
        if register_idx_len >= self.registers.len() {
            return Err(InterpreterError::InvalidRegisterIndex);
        }

        let dst = self.registers[register_idx_dst] as usize;
        let n = self.registers[register_idx_len] as usize;

        if dst >= self.memory.len() {
            return Err(InterpreterError::InvalidMemoryRange);
        }

        if dst + n > self.memory.len() {
            return Err(InterpreterError::InvalidMemoryRange);
        }

        // charge same as stdout here
        self.charge_instruction(InstructionEncoding::ReadStdout, Some(n))?;

        debug!("read stdout(reg) into mem[{:x}+{}]", dst, n);

        let _n_bytes = if let Some(tracer) = &mut self.tracer {
            tracer.read_in(&mut self.memory[dst..dst+n])
                .map_err(|err| InterpreterError::TracerError(err))?
        } else {
            error!("No tracer set in read_stdout_reg");
            0
        };

        self.pc +=
            calculate_pc_increment!(bytestream, bytecode);

        Ok(())
    }

    fn exec_submit_flag(&mut self, bytecode: &[u8]) -> InstructionExecResult {

        let mut bytestream = bytecode;

        let addr = next_u16!(bytestream) as usize;

        if addr + FLAG_LEN > self.memory.len() {
            return Err(InterpreterError::InvalidMemoryRange);
        }

        debug!("submit flag request from mem[{:x}]", addr);

        if let Some(flag) = self.flag {
            self.solved = flag.eq(&self.memory[addr..addr+FLAG_LEN]);
        }

        if self.solved {
            debug!("challenge was SOLVED!");
        } else {
            debug!("failed to solve challenge");
        }

        // hack to stop execution
        self.pc = EVM_MAX_BYTECODE_LEN;

        let _ = bytestream;

        Ok(())
    }

    pub fn is_fixed_cost_instruction(&self, instruction: InstructionEncoding)
         -> bool {

        if self.game_params.is_none() {
            return false;
        }

        // only IO instructions are variable cost
        (instruction.0 & IO_INSTR_MASK) != IO_INSTR_MASK
    }

    fn calculate_instruction_cost(&self,
                                  instruction: InstructionEncoding,
                                  quantity: Option<usize>)
         -> usize {

        if self.game_params.is_none() {
            return 0;
        }

        let game_params = self.game_params.as_ref().unwrap();

        let cost = game_params.instruction_catalogue.get(&instruction)
            .or(Some(&game_params.default_cost)).unwrap();

        match cost {
            GasCost::Fixed(val) => {
                assert!(quantity.is_none());
                *val
            },
            GasCost::Variable(val) => {
                assert!(quantity.is_some());
                val.saturating_mul(quantity.unwrap())
            },
        }
    }

    pub fn charge_instruction(&mut self,
                          instruction: InstructionEncoding,
                          quantity: Option<usize>)
         -> Result<(), InterpreterError> {

        let cost = self.calculate_instruction_cost(instruction, quantity);

        debug!("cost: {}, gas: {}", cost, self.gas);
        if cost > self.gas {
            return Err(InterpreterError::BudgetExceeded);
        }

        self.gas -= cost;

        return Ok(())
    }

    pub fn set_tracer(&mut self, tracer: Box<dyn Tracer>) {
        self.tracer = Some(tracer);
    }

    pub fn set_game_parameters(&mut self, game_params: GameParameters) {
        self.gas = game_params.budget;
        self.game_params= Some(game_params);
    }

    pub fn set_flag(&mut self, flag: &[u8; 16]) {
        let mut flag_data = [0u8; 16];
        flag_data.copy_from_slice(flag);
        self.flag = Some(flag_data);
    }

    pub fn get_gas(&self) -> usize {
        return self.gas;
    }

    pub fn get_solved(&self) -> bool {
        return self.solved
    }

    // get a copy of the register state
    pub fn get_registers(&self) -> [u64; EVM_REGISTER_COUNT] {
        self.registers
    }

    // get a copy of the memory state
    pub fn get_memory(&self) -> [u8; EVM_MEMORY_SIZE] {
        self.memory
    }

    // get a copy of the challenge elf back
    pub fn get_challenge_elf(&self) -> Vec<u8> {
        self.challenge_elf.to_vec()
    }

    pub fn dump_registers(&self) {
        for (index, value) in self.registers.iter().enumerate() {
            println!("R{index}: {value:x}");
        }
    }

    pub fn dump_memory(&self) {
        for (index, chunk) in self.memory.chunks(16).enumerate() {
            print!("{:04X}: ", index * 0x10);
            for &byte in chunk {
                print!("{:02X} ", byte);
            }
            println!();
        }
    }

    pub fn interpret_instruction(&mut self,
                                 instruction: InstructionEncoding,
                                 stream: &[u8])
         -> InstructionExecResult {

         match instruction {
            InstructionEncoding::StoreReg => {
                self.exec_store_reg(stream)
            }
            InstructionEncoding::MoveReg => {
                self.exec_move_reg(stream)
            }
            InstructionEncoding::StoreMem8 => {
                self.exec_store_mem8(stream)
            }
            InstructionEncoding::StoreMem16 => {
                self.exec_store_mem16(stream)
            }
            InstructionEncoding::StoreMem32 => {
                self.exec_store_mem32(stream)
            }
            InstructionEncoding::StoreMem64 => {
                self.exec_store_mem64(stream)
            }
            InstructionEncoding::LoadMem => {
                self.exec_load_mem(stream)
            }
            InstructionEncoding::StoreReg8 => {
                self.exec_store_reg8(stream)
            }
            InstructionEncoding::StoreReg16 => {
                self.exec_store_reg16(stream)
            }
            InstructionEncoding::StoreReg32 => {
                self.exec_store_reg32(stream)
            }
            InstructionEncoding::StoreReg64 => {
                self.exec_store_reg64(stream)
            }
            InstructionEncoding::LoadReg => {
                self.exec_load_reg(stream)
            }
            InstructionEncoding::AddReg => {
                self.exec_add_reg(stream)
            }
            InstructionEncoding::SubReg => {
                self.exec_sub_reg(stream)
            }
            InstructionEncoding::AndReg => {
                self.exec_and_reg(stream)
            }
            InstructionEncoding::LshiftReg => {
                self.exec_lshift_reg(stream)
            }
            InstructionEncoding::Jump => {
                self.exec_jump(stream)
            }
            InstructionEncoding::JumpIfEq => {
                self.exec_jump_if_eq(stream)
            }
            InstructionEncoding::JumpIfNeq => {
                self.exec_jump_if_neq(stream)
            }
            InstructionEncoding::JumpIfLt => {
                self.exec_jump_if_lt(stream)
            }
            InstructionEncoding::JumpIfGt => {
                self.exec_jump_if_gt(stream)
            }
            InstructionEncoding::WriteStdin => {
                self.exec_write_stdin(stream)
            }
            InstructionEncoding::ReadStdout => {
                self.exec_read_stdout(stream)
            }
            InstructionEncoding::WriteStdinReg => {
                self.exec_write_stdin_reg(stream)
            }
            InstructionEncoding::ReadStdoutReg => {
                self.exec_read_stdout_reg(stream)
            }
            InstructionEncoding::SubmitFlag => {
                self.exec_submit_flag(stream)
            }
            _ => { Err(InterpreterError::InvalidInstruction) }
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use std::error::Error;
    use std::collections::HashMap;

    fn get_demo_game_params(budget: usize) -> GameParameters {
        let ic = HashMap::new();

        GameParameters::new(budget, ic, GasCost::Fixed(10))
    }

    #[test]
    fn reg_access() -> Result<(), Box<dyn Error>> {
        let mut fake_elf = [0x41, 0x42, 0x43, 0x44];

        let mut interp = Interpreter::new(&mut fake_elf, None);

        let reg_moves = [
                         // mov r1, 0x0001020304050607
                         0x00, 0x01, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                         // mov r2, r1
                         0x01, 0x02, 0x01];

        interp.interpret(&reg_moves)?;

        let regs = interp.get_registers();
        assert_eq!(regs[1], 0x0001020304050607);
        assert_eq!(regs[2], 0x0001020304050607);

        Ok(())
    }

    #[test]
    fn mem_access() -> Result<(), Box<dyn Error>> {
        let mut fake_elf = [0x41, 0x42, 0x43, 0x44];

        let mut interp = Interpreter::new(&mut fake_elf, None);

        let mem_moves = [
                         // mov r1, 0x00010203
                         0x00, 0x01, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x6, 0x7,
                         // mov [0], r1b
                         0x02, 0x00, 0x00, 0x01,
                         // mov [0x100], r1w
                         0x03, 0x01, 0x00, 0x01,
                         // mov [0x200], r1
                         0x04, 0x02, 0x00, 0x01,
                         // mov r2, [0x200]
                         0x06, 0x02, 0x02, 0x00];

        interp.interpret(&mem_moves)?;

        let regs = interp.get_registers();
        assert_eq!(regs[1], 0x0001020304050607);

        let mem = interp.get_memory();
        assert_eq!(mem[0], 0x07);

        assert_eq!(mem[0x100], 0x07);
        assert_eq!(mem[0x101], 0x06);

        let val16 = u16::from_le_bytes(mem[0x100..0x102].try_into()?);
        assert_eq!(val16, 0x0607);

        assert_eq!(mem[0x200], 0x07);
        assert_eq!(mem[0x201], 0x06);
        assert_eq!(mem[0x202], 0x05);
        assert_eq!(mem[0x203], 0x04);

        let val32 = u32::from_le_bytes(mem[0x200..0x204].try_into()?);
        assert_eq!(val32, 0x04050607);

        assert_eq!(regs[2], 0x04050607);

        Ok(())
    }

    #[test]
    fn jump_if_eq() -> Result<(), Box<dyn Error>> {
        let mut fake_elf = [0x41];

        let mut interp = Interpreter::new(&mut fake_elf, None);

        let gp = get_demo_game_params(100);
        interp.set_game_parameters(gp);

        let eq_test = [
                        // move r1, 0x2
                        0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
                        // move r2, 0x2
                        0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
                        // jeq 0x28
                        0x11, 0x01, 0x02, 0x00, 0x28,
                        // move r1, 0x0
                        0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        // jump 0xfff
                        0x10, 0xff, 0xff, 0xff, 0xff,
                        // move r1, 0x1337
                        0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x37,
                      ];

        interp.interpret(&eq_test)?;

        let regs = interp.get_registers();
        assert_eq!(regs[1], 0x1337);
        let gas = interp.get_gas();
        assert_eq!(gas, 60);

        Ok(())
    }

    #[test]
    fn jump_if_neq() -> Result<(), Box<dyn Error>> {
        let mut fake_elf = [0x41];

        let mut interp = Interpreter::new(&mut fake_elf, None);

        let gp = get_demo_game_params(100);
        interp.set_game_parameters(gp);

        let neq_test = [
                        // move r1, 0x2
                        0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
                        // move r2, 0x2
                        0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
                        // jneq 0x1c
                        0x12, 0x01, 0x02, 0x00, 0x28,
                        // move r1, 0x0
                        0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        // jump 0xfff
                        0x10, 0xff, 0xff, 0xff, 0xff,
                        // move r1, 0x1337
                        0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x37,
                      ];

        interp.interpret(&neq_test)?;

        let regs = interp.get_registers();
        assert_eq!(regs[1], 0x0);
        let gas = interp.get_gas();
        assert_eq!(gas, 50);

        Ok(())
    }

    #[test]
    fn jump_if_lt() -> Result<(), Box<dyn Error>> {
        let mut fake_elf = [0x41];

        let mut interp = Interpreter::new(&mut fake_elf, None);

        let gp = get_demo_game_params(100);
        interp.set_game_parameters(gp);

        let neq_test = [
                        // move r1, 0x1
                        0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
                        // move r2, 0x100
                        0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
                        // jlt 0x1c
                        0x13, 0x01, 0x02, 0x00, 0x28,
                        // move r1, 0x0
                        0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        // jump 0xfff
                        0x10, 0xff, 0xff, 0xff, 0xff,
                        // move r1, 0x1337
                        0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x37,
                      ];

        interp.interpret(&neq_test)?;

        let regs = interp.get_registers();
        assert_eq!(regs[1], 0x1337);
        let gas = interp.get_gas();
        assert_eq!(gas, 60);

        Ok(())
    }

    #[test]
    fn jump_if_gt() -> Result<(), Box<dyn Error>> {
        let mut fake_elf = [0x41];

        let mut interp = Interpreter::new(&mut fake_elf, None);

        let gp = get_demo_game_params(100);
        interp.set_game_parameters(gp);

        let neq_test = [
                        // move r1, 0x1
                        0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
                        // move r2, 0x100
                        0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
                        // jgt 0x1c
                        0x14, 0x01, 0x02, 0x00, 0x37,
                        // move r1, 0x200
                        0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
                        // jgt 0x11
                        0x14, 0x01, 0x02, 0x00, 0x2d,
                        // jump 0xfff
                        0x10, 0xff, 0xff, 0xff, 0xff,
                        // move r1, 0x1337
                        0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x37,
                        // jump 0xfff
                        0x10, 0xff, 0xff, 0xff, 0xff,
                        // move r1, 0x0
                        0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                      ];

        interp.interpret(&neq_test)?;

        let regs = interp.get_registers();
        assert_eq!(regs[1], 0x1337);
        let gas = interp.get_gas();
        assert_eq!(gas, 30);

        Ok(())
    }

    #[test]
    fn store_reg_load_reg() -> Result<(), Box<dyn Error>> {
        let mut fake_elf = [0x41];

        let mut interp = Interpreter::new(&mut fake_elf, None);

        let gp = get_demo_game_params(100);
        interp.set_game_parameters(gp);

        let store_load = [
                            0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
                            0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
                            0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
                            0x07, 0x01, 0x02,
                            0x08, 0x01, 0x03,
                            0x0b, 0x04, 0x01,
                         ];

        interp.interpret(&store_load)?;

        let regs = interp.get_registers();
        assert_eq!(regs[1], 0x100);
        assert_eq!(regs[4], 0x200);

        let mem = interp.get_memory();
        assert_eq!(mem[0x101], 0x02);

        Ok(())
    }

    fn interpret_code(bytecode: &[u8]) -> Result<(), Box<dyn Error>> {
        // doesnt matter for how we plan to use it
        let mut fake_elf = [0x41, 0x42, 0x43, 0x44];

        let mut interp = Interpreter::new(&mut fake_elf, None);

        interp.interpret(bytecode)
    }

    #[test]
    fn reg_index_checking() -> Result<(), Box<dyn Error>> {
        let store_test = [0x00, 0xd, 0x01, 0x02, 0x03, 0x4];

        let mut result = interpret_code(&store_test);

        assert!(result.is_err());
        assert!(
            matches!((*result.err().unwrap()).downcast_ref::<InterpreterError>(),
                     Some(InterpreterError::InvalidRegisterIndex)),
            "StoreReg instruction did not validate register index"
        );

        let move_reg = [0x01, 0xf, 0x0];

        result = interpret_code(&move_reg);

        assert!(result.is_err());
        assert!(
            matches!((*result.err().unwrap()).downcast_ref::<InterpreterError>(),
                     Some(InterpreterError::InvalidRegisterIndex)),
            "MoveReg instruction did not validate register index (register 1)"
        );

        let move_reg2 = [0x01, 0x0, 0xf];

        result = interpret_code(&move_reg2);

        assert!(result.is_err());
        assert!(
            matches!((*result.err().unwrap()).downcast_ref::<InterpreterError>(),
                     Some(InterpreterError::InvalidRegisterIndex)),
            "MoveReg instruction did not validate register index (register 2)"
        );

        let store_mem8= [0x02, 0x0, 0x1, 0xc];

        result = interpret_code(&store_mem8);

        assert!(result.is_err());
        assert!(
            matches!((*result.err().unwrap()).downcast_ref::<InterpreterError>(),
                     Some(InterpreterError::InvalidRegisterIndex)),
            "StoreMem8 instruction did not validate register index"
        );

        let store_mem16 = [0x03, 0x0, 0x1, 0xc];

        result = interpret_code(&store_mem16);

        assert!(result.is_err());
        assert!(
            matches!((*result.err().unwrap()).downcast_ref::<InterpreterError>(),
                     Some(InterpreterError::InvalidRegisterIndex)),
            "StoreMem16 instruction did not validate register index"
        );

        let store_mem32 = [0x04, 0x0, 0x1, 0xc];

        result = interpret_code(&store_mem32);

        assert!(result.is_err());
        assert!(
            matches!((*result.err().unwrap()).downcast_ref::<InterpreterError>(),
                     Some(InterpreterError::InvalidRegisterIndex)),
            "StoreMem32 instruction did not validate register index"
        );

        let load_mem = [0x06, 0xc, 0x1, 0xc];

        result = interpret_code(&load_mem);

        assert!(result.is_err());
        assert!(
            matches!((*result.err().unwrap()).downcast_ref::<InterpreterError>(),
                     Some(InterpreterError::InvalidRegisterIndex)),
            "LoadMem instruction did not validate register index"
        );

        let jmp_if_eq = [0x11, 0xc, 0x1, 0xc, 0x00];

        result = interpret_code(&jmp_if_eq);

        assert!(result.is_err());
        assert!(
            matches!((*result.err().unwrap()).downcast_ref::<InterpreterError>(),
                     Some(InterpreterError::InvalidRegisterIndex)),
            "JumpIfEq instruction did not validate register index"
        );

        let jmp_if_neq = [0x12, 0x0, 0xd, 0xc, 0x00];

        result = interpret_code(&jmp_if_neq);

        assert!(result.is_err());
        assert!(
            matches!((*result.err().unwrap()).downcast_ref::<InterpreterError>(),
                     Some(InterpreterError::InvalidRegisterIndex)),
            "JumpIfNeq instruction did not validate register index"
        );

        let jmp_if_lt = [0x13, 0xe, 0x0, 0xc, 0x00];

        result = interpret_code(&jmp_if_lt);

        assert!(result.is_err());
        assert!(
            matches!((*result.err().unwrap()).downcast_ref::<InterpreterError>(),
                     Some(InterpreterError::InvalidRegisterIndex)),
            "JumpIfLt instruction did not validate register index"
        );

        let jmp_if_gt  = [0x14, 0x0, 0xf, 0xc, 0x00];

        result = interpret_code(&jmp_if_gt);

        assert!(result.is_err());
        assert!(
            matches!((*result.err().unwrap()).downcast_ref::<InterpreterError>(),
                     Some(InterpreterError::InvalidRegisterIndex)),
            "JumpIfGt instruction did not validate register index"
        );

        Ok(())
    }

    #[test]
    fn memory_address_checking() -> Result<(), Box<dyn Error>> {
        let store_mem8 = [0x2, 0x10, 0x00, 0x08];

        let mut result = interpret_code(&store_mem8);
        assert!(result.is_err());
        assert!(
            matches!((*result.err().unwrap()).downcast_ref::<InterpreterError>(),
                     Some(InterpreterError::InvalidMemoryAddress)),
            "StoreMem8 instruction did not validate dest address"
        );

        let store_mem16 = [0x3, 0x05, 0x00, 0x01];

        result = interpret_code(&store_mem16);
        assert!(result.is_err());
        assert!(
            matches!((*result.err().unwrap()).downcast_ref::<InterpreterError>(),
                     Some(InterpreterError::InvalidMemoryAddress)),
            "StoreMem16 instruction did not validate dest address"
        );

        let store_mem32 = [0x4, 0x5, 0x00, 0x01];
        result = interpret_code(&store_mem32);

        assert!(result.is_err());
        assert!(
            matches!((*result.err().unwrap()).downcast_ref::<InterpreterError>(),
                     Some(InterpreterError::InvalidMemoryAddress)),
            "StoreMem32 instruction did not validate dest address"
        );

        let load_mem = [0x5, 0x5, 0x03, 0xfd];
        result = interpret_code(&load_mem);

        assert!(result.is_err());
        assert!(
            matches!((*result.err().unwrap()).downcast_ref::<InterpreterError>(),
                     Some(InterpreterError::InvalidMemoryAddress)),
            "LoadMem instruction did not validate src address"
        );

        let write_stdin1 = [0x90, 0x02, 0x00, 0x01, 0x00];
        result = interpret_code(&write_stdin1);

        assert!(result.is_ok());

        let write_stdin2 = [0x90, 0x02, 0x00, 0x02, 0x01];
        result = interpret_code(&write_stdin2);

        assert!(result.is_err());
        assert!(
            matches!((*result.err().unwrap()).downcast_ref::<InterpreterError>(),
                     Some(InterpreterError::InvalidMemoryRange)),
            "WriteStdin instruction did not validate src address"
        );

        Ok(())
    }
}
