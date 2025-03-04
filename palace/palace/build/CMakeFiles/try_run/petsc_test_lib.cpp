#include <petsc.h>
int main()
{
  TS ts;
  int argc = 0;
  char **argv = NULL;
  PetscCall(PetscInitialize(&argc, &argv, PETSC_NULLPTR, PETSC_NULLPTR));
  PetscCall(TSCreate(PETSC_COMM_WORLD, &ts));
  PetscCall(TSSetFromOptions(ts));
  PetscCall(TSDestroy(&ts));
  PetscCall(PetscFinalize());
  return 0;
}
