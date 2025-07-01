use std::net::UdpSocket;
use std::mem;
use log;
use zerocopy::{IntoBytes, FromBytes, KnownLayout};
use std::fs::OpenOptions;
use memmap2::MmapMut;
use sha2::{Sha256, Digest};
use std::env;
use std::io::Write;
use std::net::TcpStream;

#[derive(IntoBytes, FromBytes, KnownLayout)]
#[repr(C, packed)]
struct PixelPacket {
    x: u16,          // Little-endian 16-bit integer
    y: u16,          // Little-endian 16-bit integer
}

#[derive(IntoBytes, FromBytes, KnownLayout)]
#[repr(C, packed)]
struct Challenge {
    pos: PixelPacket,
    r: u8,
    g: u8,
    b: u8,
    difficulty: u8,
    challenge: [u8; 16],
}

#[derive(IntoBytes, FromBytes, KnownLayout)]
#[repr(C, packed)]
struct SetPixelPacket {
    challenge: Challenge,
    nonce: [u8; 16],
    new_r: u8,
    new_g: u8,
    new_b: u8,
}

const WIDTH: u16 = 384;
const HEIGHT: u16 = 256;

impl PartialEq for Challenge {
    fn eq(&self, other: &Self) -> bool {
        self.pos.x == other.pos.x && self.pos.y == other.pos.y && self.r == other.r && self.g == other.g && self.b == other.b &&
        self.difficulty == other.difficulty && self.challenge == other.challenge
    }
}

const CHALLENGE_SIZE: usize = core::mem::size_of::<Challenge>();

fn send_pixels(px_states: &mut [Challenge]) {
    // Example processing function
    let mut stream = TcpStream::connect("172.29.165.125:5000").expect("Failed to connect to TCP server");
    let mut data: [u8;  WIDTH as usize * HEIGHT as usize * 4] = [0; WIDTH as usize * HEIGHT as usize * 4];

    loop {
        let mut i = 0;
        for px_state in px_states.iter() {
            data[i * 4] = px_state.r;
            data[i * 4 + 1] = px_state.g;
            data[i * 4 + 2] = px_state.b;

            i += 1;
        }

        if let Err(e) = stream.write_all(&data) {
            log::error!("Failed to send pixel: {}", e);
            break;
        }

        std::thread::sleep(std::time::Duration::from_secs_f64(1.0 / 60.0));
    }
}

fn main() {
    let file_path = "/tmp/pixel-chain";
    let file = OpenOptions::new()
        .read(true)
        .write(true)
        .create(true)
        .open(file_path)
        .expect("Failed to open or create file");

    let file_size = (WIDTH as u64) * (HEIGHT as u64) * CHALLENGE_SIZE as u64;
    file.set_len(file_size).expect("Failed to set file length");

    let mut mmap = unsafe {
        MmapMut::map_mut(&file).expect("Failed to mmap the file")
    };
    let (_, px_states, _) = unsafe {
        mmap.align_to_mut::<Challenge>()
    };

    for y in 0..HEIGHT {
        for x in 0..WIDTH {
            let offset = y as usize * WIDTH as usize + x as usize;
            px_states[offset].pos.x = x;
            px_states[offset].pos.y = y;
            px_states[offset].difficulty = 10;
        }
    }

    let args: Vec<String> = env::args().collect();
    if args.len() > 1 && args[1] == "--send" {
        return send_pixels(px_states);
    }

    let socket = UdpSocket::bind("0.0.0.0:8080").expect("Couldn't bind to address");

    let mut packet: SetPixelPacket = unsafe { mem::zeroed() };

    loop {
        let packet_bytes: &mut [u8; core::mem::size_of::<SetPixelPacket>()] =
            zerocopy::transmute_mut!(&mut packet);
        match socket.recv_from(packet_bytes) {
            Ok((size, src)) => {
                if size < 4 || packet.challenge.pos.x >= WIDTH || packet.challenge.pos.y >= HEIGHT {
                    continue;
                }

                let offset = packet.challenge.pos.y as usize * WIDTH as usize + packet.challenge.pos.x as usize;

                if size == core::mem::size_of::<SetPixelPacket>() {
                    let _packet_bytes: &mut [u8; core::mem::size_of::<SetPixelPacket>()] =
                        zerocopy::transmute_mut!(&mut packet);

                    if px_states[offset] == packet.challenge {
                        let packet_bytes: &mut [u8; core::mem::size_of::<SetPixelPacket>()] =
                            zerocopy::transmute_mut!(&mut packet);
                        let mut hasher = Sha256::new();
                        // With the nonce
                        hasher.update(&packet_bytes[..core::mem::size_of::<Challenge>() + 16]);
                        let hash_result = hasher.finalize();

                        let leading_u64 = u64::from_le_bytes(hash_result[..8].try_into().unwrap());

                        println!("leading_u64 {:x}, hash: {:?}", leading_u64, hash_result);

                        if 0 == (leading_u64 & (((1 as u64) << packet.challenge.difficulty)-1)) {
                            println!("good!");
                            px_states[offset].r = packet.new_r;
                            px_states[offset].g = packet.new_g;
                            px_states[offset].b = packet.new_b;
                            px_states[offset].challenge = packet.nonce;
                        }
                    }
                }

                let tx_packet_bytes: &mut [u8; core::mem::size_of::<Challenge>()] =
                    zerocopy::transmute_mut!(&mut px_states[offset]);

                if let Err(e) = socket.send_to(tx_packet_bytes, src) {
                    log::error!("Failed to send response: {}", e);
                }
            }
            Err(e) => {
                log::error!("Error receiving data: {}", e);
            }
        }
    }
}
