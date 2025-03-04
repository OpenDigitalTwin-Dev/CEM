#include <iostream>
#if defined(__cpp_lib_filesystem) ||     defined(__has_include) && __has_include(<filesystem>)
#include <filesystem>
namespace fs = std::filesystem;
#else
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#endif
int main()
{
  std::cout << "Current path is " << fs::current_path() << '\n';
  return 0;
}
