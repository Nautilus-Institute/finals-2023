pub mod tracer;
pub mod launcher;
pub mod interpreter;
pub mod sysless_tracer;
#[cfg(feature = "game_1")]
pub mod game_1;
#[cfg(feature = "game_2")]
pub mod game_2;
#[cfg(feature = "game_3")]
pub mod game_3;
#[cfg(test)]
pub mod fake_tracer;

const FLAG_LEN: usize = 16;
