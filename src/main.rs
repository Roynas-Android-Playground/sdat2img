use std::collections::BTreeMap;
use std::env;
use std::error::Error;
use std::fmt;
use std::fs::{self, File};
use std::io::{self, BufRead, BufReader, Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};

const DEFAULT_OUTPUT: &str = "system.img";
const BLOCK_SIZE: usize = 4096;

type FileSizeT = usize;

#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord)]
enum Command {
    Erase,
    New,
    Zero,
}

#[derive(Debug)]
struct ByteSegments {
    begin: FileSizeT,
    end: FileSizeT,
}

impl ByteSegments {
    fn write_to_file(&self, input: &mut dyn Read, output: &mut dyn Write) -> io::Result<()> {
        let block_count = self.end - self.begin;
        println!("Copying {} blocks into position {}...", block_count, self.begin);
        
        for _ in 0..block_count {
            let mut buffer = vec![0u8; BLOCK_SIZE];
            input.read_exact(&mut buffer)?;
            output.write_all(&buffer)?;
        }
        
        Ok(())
    }

    fn size(&self) -> FileSizeT {
        self.end - self.begin
    }
}

#[derive(Debug)]
struct TransferList {
    version: u32,
    commands: BTreeMap<Command, Vec<ByteSegments>>,
}

impl TransferList {
    fn parse(transfer_list_file: &Path) -> Result<Self, Box<dyn Error>> {
        let file = File::open(transfer_list_file)?;
        let reader = BufReader::new(file);
        
        let mut lines = reader.lines().filter_map(Result::ok);
        
        let version: u32 = lines.next().ok_or("Failed to read version")?.parse()?;
        println!("Detected version: {}", version);
       
        lines.next();
        // Skip irrelevant lines based on version
        if version >= 2 {
            lines.next();
            lines.next();
        }
        
        let mut commands = BTreeMap::new();
        
        for line in lines {
            let parts: Vec<&str> = line.split_whitespace().collect();
            if parts.len() != 2 {
                return Err(Box::new(TextFileError::new("Invalid command format")));
            }
            
            let command = TransferList::to_operations(parts[0])?;
            let nums = parse_ranges(parts[1])?;
            
            for chunk in nums.chunks(2) {
                if chunk.len() == 2 {
                    let segment = ByteSegments {
                        begin: chunk[0],
                        end: chunk[1],
                    };
                    commands.entry(command.clone()).or_insert(Vec::new()).push(segment);
                }
            }
        }
        
        Ok(Self { version, commands })
    }
    
    fn to_operations(command: &str) -> Result<Command, Box<dyn Error>> {
        match command {
            "erase" => Ok(Command::Erase),
            "new" => Ok(Command::New),
            "zero" => Ok(Command::Zero),
            _ => Err(Box::new(TextFileError::new(&format!("Invalid operation: {}", command)))),
        }
    }
    
    fn max(&self) -> FileSizeT {
        self.commands
            .values()
            .flat_map(|segments| segments.iter())
            .map(|segment| segment.end)
            .max()
            .unwrap_or(0)
    }
    
    fn for_each_command<F>(&self, mut callback: F)
    where
        F: FnMut(&Command, &ByteSegments),
    {
        for (cmd, segments) in &self.commands {
            for segment in segments {
                callback(cmd, segment);
            }
        }
    }
}

#[derive(Debug)]
struct TextFileError {
    message: String,
}

impl TextFileError {
    fn new(message: &str) -> Self {
        TextFileError {
            message: message.to_string(),
        }
    }
}

impl fmt::Display for TextFileError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.message)
    }
}

impl Error for TextFileError {}

fn parse_ranges(src: &str) -> Result<Vec<FileSizeT>, Box<dyn Error>> {
    let src_set: Vec<&str> = src.split(',').collect();
    let mut ret: Vec<FileSizeT> = Vec::new();
    
    for s in src_set {
        ret.push(s.parse()?);
    }
    
    if ret.len() != ret[0] + 1 {
        return Err(Box::new(TextFileError::new("Range size mismatch")));
    }
    
    ret.remove(0);
    
    if ret.len() % 2 != 0 {
        return Err(Box::new(TextFileError::new("Range length is not even")));
    }
    
    Ok(ret)
}

fn usage(exe: &str) -> ! {
    println!("Usage: {} <transfer_list> <system_new_file> <system_img>", exe);
    println!("    <transfer_list>: transfer list file");
    println!("    <system_new_file>: system new dat file");
    println!("    <system_img>: output system image");
    println!("If you are lazy, then just provide directory and filename, I will try to auto detect them.");
    std::process::exit(1);
}

fn main() -> Result<(), Box<dyn Error>> {
    let args: Vec<String> = env::args().collect();
    if args.len() != 4 && args.len() != 3 {
        usage(&args[0]);
    }
    
    let transfer_list_file;
    let new_dat_file;
    let output_img;
    
    if Path::new(&args[1]).is_file() {
        transfer_list_file = PathBuf::from(&args[1]);
        new_dat_file = PathBuf::from(&args[2]);
        output_img = if args.len() == 3 {
            PathBuf::from(DEFAULT_OUTPUT)
        } else {
            PathBuf::from(&args[3])
        };
    } else if Path::new(&args[1]).is_dir() {
        let dir = Path::new(&args[1]);
        let common_prefix = &args[2];
        transfer_list_file = dir.join(format!("{}.transfer.list", common_prefix));
        new_dat_file = dir.join(format!("{}.new.dat", common_prefix));
        output_img = if args.len() == 3 {
            dir.join(format!("{}.img", common_prefix))
        } else {
            PathBuf::from(&args[3])
        };
    } else {
        usage(&args[0]);
    }
    
    let transfer_list = TransferList::parse(&transfer_list_file)?;
    
    if output_img.exists() {
        eprintln!("Error: The output file {} already exists.", output_img.display());
        print!("Do you want to overwrite it? (y/N): ");
        io::stdout().flush()?;
        
        let mut answer = String::new();
        io::stdin().read_line(&mut answer)?;
        
        if answer.trim().to_lowercase() != "y" {
            eprintln!("Aborting...");
            return Ok(());
        }
        
        fs::remove_file(&output_img)?;
    }
    
    let mut output = File::create(&output_img)?;
    let mut input_dat = File::open(&new_dat_file)?;
    
    let max_file_size = transfer_list.max() * BLOCK_SIZE;
    println!("New file size: {} bytes", max_file_size);
    
    transfer_list.for_each_command(|cmd, seg| match cmd {
        Command::New => {
            if let Err(e) = seg.write_to_file(&mut input_dat, &mut output) {
                eprintln!("Error writing to file: {}", e);
            }
        }
        _ => {
            println!("Skipping command {:?}", cmd);
        }
    });
    
    output.set_len(max_file_size as u64)?;
    println!("Done! Output image: {}", output_img.display());
    
    Ok(())
}

