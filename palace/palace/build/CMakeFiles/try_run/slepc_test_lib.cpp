#include <petsc.h>
#include <slepc.h>
int main()
{
  EPS eps;
  int argc = 0;
  char **argv = NULL;
  PetscCall(SlepcInitialize(&argc, &argv, PETSC_NULLPTR, PETSC_NULLPTR));
  PetscCall(EPSCreate(PETSC_COMM_SELF, &eps));
  PetscCall(EPSDestroy(&eps));
  PetscCall(SlepcFinalize());
  return 0;
}
