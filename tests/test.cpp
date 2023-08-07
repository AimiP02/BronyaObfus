#include <Windows.h>
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <process.h>

class Crypto {
public:
  Crypto(const char *Key) : Key(Key) {
    for (int i = 0; i < 256; i++) {
      S[i] = i;
    }

    for (int i = 0, j = 0; i < 256; i++) {
      j = (j + (S[i] & 0xFF) + ((BYTE)Key[i % strlen(Key)] & 0xFF)) % 256;
      std::swap(S[i], S[j]);
    }
  }
  ~Crypto() = default;

  char *Encrypt(const char *data, int len);

public:
  const char *Key;

private:
  BYTE S[256];
};

namespace base64 {

inline std::string get_base64_chars() {
  static std::string base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                    "abcdefghijklmnopqrstuvwxyz"
                                    "0123456789+/";
  return base64_chars;
}

inline std::string to_base64(std::string const &data) {
  int counter = 0;
  uint32_t bit_stream = 0;
  const std::string base64_chars = get_base64_chars();
  std::string encoded;
  int offset = 0;
  for (unsigned char c : data) {
    auto num_val = static_cast<unsigned int>(c);
    offset = 16 - counter % 3 * 8;
    bit_stream += num_val << offset;
    if (offset == 16) {
      encoded += base64_chars.at(bit_stream >> 18 & 0x3f);
    }
    if (offset == 8) {
      encoded += base64_chars.at(bit_stream >> 12 & 0x3f);
    }
    if (offset == 0 && counter != 3) {
      encoded += base64_chars.at(bit_stream >> 6 & 0x3f);
      encoded += base64_chars.at(bit_stream & 0x3f);
      bit_stream = 0;
    }
    counter++;
  }
  if (offset == 16) {
    encoded += base64_chars.at(bit_stream >> 12 & 0x3f);
    encoded += "==";
  }
  if (offset == 8) {
    encoded += base64_chars.at(bit_stream >> 6 & 0x3f);
    encoded += '=';
  }
  return encoded;
}

inline std::string from_base64(std::string const &data) {
  int counter = 0;
  uint32_t bit_stream = 0;
  std::string decoded;
  int offset = 0;
  const std::string base64_chars = get_base64_chars();
  for (unsigned char c : data) {
    auto num_val = base64_chars.find(c);
    if (num_val != std::string::npos) {
      offset = 18 - counter % 4 * 6;
      bit_stream += num_val << offset;
      if (offset == 12) {
        decoded += static_cast<char>(bit_stream >> 16 & 0xff);
      }
      if (offset == 6) {
        decoded += static_cast<char>(bit_stream >> 8 & 0xff);
      }
      if (offset == 0 && counter != 4) {
        decoded += static_cast<char>(bit_stream & 0xff);
        bit_stream = 0;
      }
    } else if (c != '=') {
      return std::string();
    }
    counter++;
  }
  return decoded;
}

} // namespace base64

char *Crypto::Encrypt(const char *data, int len) {
  char *out = new char[len];

  int x = 0, y = 0;
  for (int i = 0; i < len; i++) {
    x = (x + 1) % 256;
    y = (y + S[x]) % 256;
    std::swap(S[x], S[y]);
    BYTE key = S[(S[x] + S[y]) % 256];
    out[i] = data[i] ^ key;
  }

  return out;
}

void check(char *Cipher) {
  auto base64_cipher = base64::to_base64(Cipher);
  auto your_flag = base64::to_base64(base64_cipher);

  if (!strcmp(your_flag.c_str(), "R0hOeUpmVE52dXR5SUpjMUhRPT0=")) {
    std::cout << "Done.\n";
  } else {
    std::cout << "Not this flag.\n";
  }
}

unsigned int __stdcall AntiDebug(PVOID pM) {
  while (true) {
    if (IsDebuggerPresent()) {
      // MessageBoxA(NULL, "AntiDebug", "AntiDebug", MB_OK);
      printf("No Debugger!\n");
      ExitProcess(0);
      return 0;
    }
  }

  return 0;
}

int main() {
  HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, AntiDebug, NULL, 0, NULL);

  if (hThread == NULL) {
    printf("_beginthreadex failed!\n");
    ExitProcess(0);
  }

  char Flag[20];
  printf("Input flag: ");
  std::cin >> Flag;

  Crypto *crypto = new Crypto("BronyaZaychik");

  // _beginthread(reinterpret_cast<_beginthread_proc_type>(ThreadFunc), 0,
  // &crypto);

  char *cEncryptData = crypto->Encrypt(Flag, strlen(Flag));

  check(cEncryptData);

  system("pause");

  return 0;
}