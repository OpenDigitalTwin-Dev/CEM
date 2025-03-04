#include <ceed.h>
#include <ceed/backend.h>
int main()
{
  Ceed ceed;
  CeedCall(CeedInit("/cpu/self", &ceed));
  CeedCall(CeedDestroy(&ceed));
  return 0;
}
