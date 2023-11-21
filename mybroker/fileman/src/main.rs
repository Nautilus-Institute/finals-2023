use fileman::ThreadPool;

use std::{
    fs,
    env,
    path::Path,
    io::{prelude::*, BufReader},
    net::{TcpListener, TcpStream},
};

fn main() {
    let listener = TcpListener::bind("::1:8903").unwrap();
    let pool = ThreadPool::new(4);

    for stream in listener.incoming() {
        let stream = stream.unwrap();

        pool.execute(|| {
            handle_connection(stream);
        });
    }
}


fn handle_connection(mut stream: TcpStream) {
    let buf_reader = BufReader::new(&mut stream);
    let http_request: Vec<_> = buf_reader
        .lines()
        .map(|result| result.unwrap())
        .take_while(|line| !line.is_empty())
        .collect();

    // Parse the request
    let parts = http_request[0].split(" ")
        .collect::<Vec<&str>>();
    if parts[0] != "GET" {
        let status_line = "HTTP/1.1 400 Unsupported Method";
        let response = format!("{status_line}\r\n\r\n");
        stream.write_all(response.as_bytes()).unwrap();
        return;
    }
    if parts.len() <= 2 {
        let status_line = "HTTP/1.1 400 Error";
        let response = format!("{status_line}\r\n\r\n");
        stream.write_all(response.as_bytes()).unwrap();
        return;
    }
    if parts[1].contains("..") {
        let status_line = "HTTP/1.1 420 Unsupported Path";
        let response = format!("{status_line}\r\n\r\n");
        stream.write_all(response.as_bytes()).unwrap();
        return;
    }

    // Load the file as expected

    // Remove the leading "/"
    // VULN: You can force read from an absolute path by passing in enough backslashes
    let ch0 = parts[1].chars().next().unwrap();
    let file_path_0 = if ch0 == '/' { &parts[1][1..] } else { parts[1] };
    let ch1 = file_path_0.chars().next().unwrap();
    let file_path_1 = if ch1 == '/' { &file_path_0[1..] } else { file_path_0 };
    let ch2 = file_path_1.chars().next().unwrap();
    let file_path_2 = if ch2 == '/' { &file_path_1[1..] } else { file_path_1 };
    let file_path = file_path_2;


    // chdir to /tmp
    let root = Path::new("/tmp");
    env::set_current_dir(&root).unwrap();

    let status_line = "HTTP/1.1 200 OK";

    // VULN: prefix the path with "/" if we found "?a1"
    let has_a1 = parts[1].ends_with("?a1");
    let a1_stripped = &parts[1][0..parts[1].len() - 3];
    let real_file_path = if has_a1 { format!("/{a1_stripped}") } else { file_path.to_string() };

    let contents = fs::read_to_string(real_file_path);
    match contents {
        Ok(contents) => {
            let length = contents.len();
            let response = format!("{status_line}\r\nConnection: Close\r\nContent-Length: {length}\r\n\r\n{contents}");
            stream.write_all(response.as_bytes()).unwrap();
        }
        Err(_) => {
            let response = format!("{status_line}\r\nConnection: Close\r\nContent-Length: 0\r\n\r\n");
            stream.write_all(response.as_bytes()).unwrap();
        }
    }
}
