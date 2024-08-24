/*
 * This is a C++ equivalent version of the original sdat2img, which was
 * originally written in Python by xpirt, luxi78, and howellzhu.
 *
 */

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef HAVE_BROTLI
#include <brotli/decode.h>
#endif

constexpr static std::string_view DEFAULT_OUTPUT = "system.img";
constexpr static int BLOCK_SIZE = 4096;
using FileSizeT = size_t;

// Represents the transfer.list file
struct TransferList {
  enum class Command { Erase, New, Zero };
  struct ByteSegments;
  using OperationsList = std::multimap<Command, ByteSegments>;
  using ForEachCommand = std::function<void(Command, const ByteSegments &)>;

  struct ByteSegments {
  private:
    FileSizeT _begin;
    FileSizeT _end;

  public:
    ByteSegments(FileSizeT begin, FileSizeT end) : _begin(begin), _end(end) {}

    void writeToFile(std::istream &in, std::ostream &out) const {
      FileSizeT block_count = _end - _begin;
      std::cout << "Copying " << block_count << " blocks into position "
                << _begin << "..." << std::endl;
      out.seekp(_begin * BLOCK_SIZE, std::ios::beg);
      while (block_count > 0) {
        std::array<char, BLOCK_SIZE> buffer{};
        in.read(buffer.data(), BLOCK_SIZE);
        out.write(buffer.data(), BLOCK_SIZE);
        block_count--;
      }
    }

    FileSizeT end() const noexcept { return _end; }
    FileSizeT begin() const noexcept { return _begin; }
    FileSizeT size() const noexcept { return _end - _begin; }
  };

private:
  // Version of the transfer.list scheme.
  int version;
  // Commands list
  OperationsList commands;

public:
  // parser taking a transfer list file path.
  void parse(const std::filesystem::path &transfer_list_file);
  void forEachCommand(const ForEachCommand &callbacks);
  FileSizeT max();

  // Convert string to Operations, throwing an error if invalid.
  static Command toOperations(const std::string &command) {
    if (command == "erase") {
      return Command::Erase;
    } else if (command == "new") {
      return Command::New;
    } else if (command == "zero") {
      return Command::Zero;
    } else {
      throw std::invalid_argument("Invalid operation: " + command);
    }
  }
};

// std::ostream operator for TransferList::Command enum.
std::ostream &operator<<(std::ostream &self,
                         const TransferList::Command &operation) {
  switch (operation) {
  case TransferList::Command::Erase:
    return self << "erase";
  case TransferList::Command::New:
    return self << "new";
  case TransferList::Command::Zero:
    return self << "zero";
  }
  return self;
}

// Declare a exception within file operations failure
class IOException : public std::runtime_error {
public:
  explicit IOException(const std::filesystem::path &path,
                       const std::string &message)
      : std::runtime_error("Couldn't " + message + " file: " + path.string()) {}
};

// Represent a text file with lines
struct TextFile {
private:
  std::ifstream file;
  int line_num;
  std::filesystem::path path;

public:
  explicit TextFile(const std::filesystem::path &path)
      : file(path), line_num(0), path(path) {
    if (!file.is_open()) {
      throw IOException(path, "open");
    }
  }

  template <typename T = std::string> bool takeOneLine(T *out) {
    std::string line;
    std::stringstream stream;
    if (!getline(file, line)) {
      return false;
    }
    ++line_num;
    if constexpr (std::is_same_v<T, std::string>) {
      *out = line;
      return true;
    } else {
      stream << line;
      if (stream >> *out) {
        return true;
      }
      std::cerr << "Couldn't convert line to type T";
      return false;
    }
  }
  void ignoreLine() {
    std::string line;
    if (getline(file, line)) {
      ++line_num;
    }
  }
  template <int X> void ignoreLine() {
    for (int i = 0; i < X; ++i) {
      ignoreLine();
    }
  }

  std::string current() const noexcept {
    std::stringstream stream;
    stream << "Line " << line_num << " of file: " << path;
    return stream.str();
  }

  // Disable move constructors
  TextFile(TextFile &&) = delete;
  TextFile &operator=(TextFile &&) = delete;
};

// Create exception with the TextFile object
class TextFileError : public std::runtime_error {
public:
  explicit TextFileError(const TextFile &file, const std::string &message)
      : std::runtime_error(message + ". Parser is at " + file.current()) {}
};

// Helper function like in GTest.
template <typename IntT>
void expected_eq(const std::string_view expection, const IntT l_op,
                 const IntT r_op) {
  std::cerr << "Expected " << expection << ", but " << l_op << " != " << r_op
            << std::endl;
}
#define EXPECTED_EQ(l_op, r_op) expected_eq(#l_op " == " #r_op, l_op, r_op)
#define ABORT_PARSING_IF(tfile, cond)                                          \
  if ((cond)) {                                                                \
    throw TextFileError(tfile,                                                 \
                        "Couldn't parse line, " #cond " condition has met");   \
  }

std::vector<std::string> split(const std::string &str, const char &delimiter) {
  std::vector<std::string> tokens;
  std::stringstream ss(str);
  std::string token;
  while (getline(ss, token, delimiter)) {
    tokens.emplace_back(token);
  }
  return tokens;
}

std::vector<FileSizeT> parseRanges(const std::string &src) {
  std::vector<std::string> src_set = split(src, ',');
  std::vector<FileSizeT> ret;

  std::transform(src_set.begin(), src_set.end(), std::back_inserter(ret),
                 [&src_set](const auto &src) {
                   FileSizeT num;
                   std::stringstream ss(src);
                   if (!(ss >> num)) {
                     throw std::invalid_argument(
                         "Error parsing following data to rangeset: " + src);
                   }
                   return num;
                 });
  if (ret.size() != ret[0] + 1) {
    EXPECTED_EQ(ret.size(), static_cast<size_t>(ret[0] + 1));
    return {};
  }
  if ((ret.size() - 1) % 2 != 0) {
    EXPECTED_EQ(ret.size() % 2, static_cast<size_t>(0));
    return {};
  }
  // Remove first element
  ret.erase(ret.begin());
  return ret;
}

void TransferList::parse(const std::filesystem::path &transfer_list_file) {
  std::string line;
  std::vector<FileSizeT> nums;
  TextFile transfer_list(transfer_list_file);

  // First line is the version
  if (!transfer_list.takeOneLine(&version)) {
    throw TextFileError(transfer_list, "Failed to read version");
  }
  switch (version) {
  case 1:
    std::cout << "Android 5.0 detected" << std::endl;
    break;
  case 2:
    std::cout << "Android 5.1 detected" << std::endl;
    break;
  case 3:
    std::cout << "Android 6.x detected" << std::endl;
    break;
  case 4:
    std::cout << "Android 7.x or above detected" << std::endl;
    break;
  default:
    throw TextFileError(transfer_list,
                        "Unknown version: " + std::to_string(version));
  }

  // Second line is total number of blocks. Ignore it though.
  // We are going to calculate it by ourselves.
  transfer_list.ignoreLine();

  // Skip those 2 lines if version >= 2
  if (version >= 2) {
    transfer_list.ignoreLine<2>();
  }

  // Loop through all lines
  while (transfer_list.takeOneLine(&line)) {
    const auto &split_line = split(line, ' ');
    ABORT_PARSING_IF(transfer_list, split_line.size() != 2);
    nums = parseRanges(split_line[1]);
    ABORT_PARSING_IF(transfer_list, nums.empty());

    for (size_t i = 0; i < nums.size(); i += 2) {
      commands.emplace(toOperations(split_line[0]),
                       TransferList::ByteSegments(nums[i], nums[i + 1]));
    }
  }
  std::cout << "Parsed " << commands.size() << " commands" << std::endl;
}

void TransferList::forEachCommand(const ForEachCommand &callbacks) {
  // Commands is reversed, so we need to use cbegin, cend
  for (const auto &it : commands) {
    callbacks(it.first, it.second);
  }
}

FileSizeT TransferList::max() {
  return std::max_element(commands.begin(), commands.end(),
                          [](const auto &a, const auto &b) {
                            return a.second.end() < b.second.end();
                          })
      ->second.end();
}

[[noreturn]] void usage(const char *exe) {
  std::cout << "Usage: " << exe
            << " <transfer_list> <system_new_file> <system_img>" << std::endl;
  std::cout << "    <transfer_list>: transfer list file" << std::endl;
  std::cout << "    <system_new_file>: system new dat file ";
#ifdef HAVE_BROTLI
  std::cout << "(Can support brotli compressed)";
#endif
  std::cout << std::endl;
  std::cout << "    <system_img>: output system image" << std::endl;
  exit(EXIT_SUCCESS);
}

#ifdef HAVE_BROTLI

class BrotliManager {
public:
  BrotliManager(const std::filesystem::path &input_file)
      : file_path(input_file) {}

  bool isValidBrotli() const {
    // TODO: Check actual, for now we are doing the same as the brotli executable does
    // Checking the br file extension.
    return file_path.filename().extension() == ".br";
  }
  bool decompress(const std::filesystem::path &output_file) const {
    // Open the input file in binary mode
    std::ifstream file(file_path, std::ios::binary | std::ios::ate);

    if (!file.is_open()) {
      std::cerr << "Error opening input file: " << file_path << std::endl;
      return false;
    }

    // Get the size of the file and read the content
    std::ifstream::pos_type file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> compressed_data(file_size);
    file.read(reinterpret_cast<char *>(compressed_data.data()), file_size);

    if (!file) {
      std::cerr << "Error reading input file: " << file_path << std::endl;
      return false;
    }

    // Initialize the Brotli decoder
    BrotliDecoderState *state =
        BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);
    if (!state) {
      std::cerr << "Error creating Brotli decoder state." << std::endl;
      return false;
    }

    // Prepare output file
    std::ofstream output(output_file, std::ios::binary);
    if (!output.is_open()) {
      std::cerr << "Error opening output file: " << output_file << std::endl;
      BrotliDecoderDestroyInstance(state);
      return false;
    }

    // Decompression buffer
    const size_t kBufferSize = 4096;
    std::vector<uint8_t> output_buffer(kBufferSize);

    size_t input_pos = 0;
    size_t available_out = kBufferSize;
    uint8_t *output_ptr = output_buffer.data();

    // Decompress the data
    BrotliDecoderResult result = BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT;

    while (result != BROTLI_DECODER_RESULT_SUCCESS &&
           result != BROTLI_DECODER_RESULT_ERROR) {
      size_t available_in = compressed_data.size() - input_pos;
      const uint8_t *next_in = compressed_data.data() + input_pos;
      result = BrotliDecoderDecompressStream(
          state, &available_in, &next_in, &available_out, &output_ptr, nullptr);

      // Write the decompressed data to the output file
      if (output_ptr != output_buffer.data()) {
        output.write(reinterpret_cast<char *>(output_buffer.data()),
                     kBufferSize - available_out);
        available_out = kBufferSize;
        output_ptr = output_buffer.data();
      }

      // Move the input position
      input_pos += (compressed_data.size() - available_in - input_pos);
    }

    // Final check for success
    if (result == BROTLI_DECODER_RESULT_SUCCESS) {
      std::cout << "Decompression successful." << std::endl;
    } else {
      std::cerr << "Decompression failed with error code: "
                << BrotliDecoderGetErrorCode(state) << std::endl;
    }

    // Clean up
    BrotliDecoderDestroyInstance(state);
    return result == BROTLI_DECODER_RESULT_SUCCESS;
  }

private:
  std::filesystem::path file_path;
};

#endif

int main(int argc, const char *argv[]) {
  std::filesystem::path transfer_list_file, new_dat_file, output_img;
  int block_count;

  if (argc != 4 && argc != 3) {
    usage(argv[0]);
  }

  transfer_list_file = argv[1];
  new_dat_file = argv[2];
  if (argc == 3) {
    output_img = DEFAULT_OUTPUT;
  } else {
    output_img = argv[3];
  }

#ifdef HAVE_BROTLI
  BrotliManager brotli_manager(new_dat_file);
  if (!brotli_manager.isValidBrotli()) {
    std::cerr << "Warning: The input file " << new_dat_file
              << " is not a valid Brotli-compressed file." << std::endl;
  } else {
    std::cout << "Decompressing Brotli-compressed file in-line... ";
    if (!brotli_manager.decompress(new_dat_file)) {
      std::cerr << "Failed." << std::endl;
      return EXIT_FAILURE;
    }
    std::cout << "Done." << std::endl;
  }
#endif

  TransferList tlist;

  try {
    tlist.parse(transfer_list_file);
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  std::error_code ec;
  if (std::filesystem::exists(output_img, ec)) {
    std::cerr << "Error: The output file " << output_img << " already exists."
              << std::endl;

    std::cout << "Do you want to overwrite it? (y/N): ";
    std::string answer;
    std::cin >> answer;
    if (answer != "y" && answer != "Y") {
      std::cerr << "Aborting..." << std::endl;
      return EXIT_FAILURE;
    } else {
      std::filesystem::remove(output_img, ec);
      if (ec) {
        std::cerr << "Error: Could not remove file " << output_img << ": "
                  << ec.message() << std::endl;
        return EXIT_FAILURE;
      }
    }
  }

  std::ofstream output(output_img, std::ios::binary);
  if (!output) {
    std::cerr << "Error: Could not open file " << output_img << std::endl;
    return EXIT_FAILURE;
  }

  std::ifstream input_dat(new_dat_file, std::ios::binary);
  if (!input_dat) {
    std::cerr << "Error: Could not open file " << new_dat_file << std::endl;
    return EXIT_FAILURE;
  }

  // Calculate total number of blocks
  FileSizeT max_file_size = tlist.max() * BLOCK_SIZE;
  std::cout << "New file size: " << max_file_size << " bytes" << std::endl;

  tlist.forEachCommand([&](const TransferList::Command c,
                           const TransferList::ByteSegments &seg) {
    switch (c) {
    case TransferList::Command::New: {
      seg.writeToFile(input_dat, output);
      break;
    }
    default:
      std::cout << "Skipping command " << c << "..." << std::endl;
    }
  });

  output.close();
  input_dat.close();

  std::filesystem::resize_file(output_img, max_file_size);
  std::cout << "Done! Output image: " << output_img << std::endl;

  return 0;
}
