#include <cmath>
int main()
{
  constexpr double two = 2.0;
  constexpr double four = two*two;
  constexpr double sqrtfour = std::sqrt(four);
  return 0;
}
