use clap::Parser;

use std::{
    process::ExitCode,
    path::PathBuf,
};

use nix::unistd::alarm;

use evm::launcher::read_bytecode;

#[cfg(feature = "game_1")]
use evm::game_1::play_game_1 as play_game;
#[cfg(feature = "game_2")]
use evm::game_2::play_game_2 as play_game;
#[cfg(feature = "game_3")]
use evm::game_3::play_game_3 as play_game;

#[derive(Parser, Debug)]
struct Args {
    #[arg(long)]
    bytecode_file: Option<PathBuf>,

    #[arg(long)]
    no_sandbox: bool,

    #[arg(long)]
    no_alarm: bool,

    #[arg(long, default_value_t = 5)]
    alarm_secs: u32,

    #[arg(long, action = clap::ArgAction::Append)]
    artifact: Vec<PathBuf>,

    #[arg(required = true, num_args=1..)]
    command: Vec<String>,
}

fn main() {
    env_logger::init();

    let args = Args::parse();

    if !args.no_alarm {
        alarm::set(args.alarm_secs);
    }

    let bytecode = read_bytecode(args.bytecode_file)
        .expect("accept bytecode from player");

    match play_game(args.command, args.no_sandbox, args.artifact, bytecode) {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("Error: {e}");
            ExitCode::FAILURE
        }
    };
}
