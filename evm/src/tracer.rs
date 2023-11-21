use thiserror::Error;
use std::io::Error as IoError;

use nix;

#[derive(Debug, Error)]
pub enum TracerError {
    #[error("Invalid address for inferior process")]
    InvalidInferiorAddress,
    #[error("Unknown memory error accessing inferior process")]
    InferiorMemoryAccessError,
    #[error("I/O error {0}")]
    Io(IoError),
    #[error("OS error")]
    Os(#[from] nix::Error),
}

pub trait Tracer {
    fn write_out(self: &mut Self, buf: &[u8]) -> Result<usize, TracerError>;

    fn read_in(self: &mut Self, buf: &mut [u8]) -> Result<usize, TracerError>;
}
