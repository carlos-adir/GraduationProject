static char help[] = "A structured-grid Poisson solver using DMDA+KSP.\n\n";

#include <petsc.h>

extern PetscErrorCode formMatrix(DM, Mat);
extern PetscErrorCode formExact(DM, Vec);
extern PetscErrorCode formRHS(DM, Vec);

//STARTMAIN
int main(int argc,char **args) {
    PetscErrorCode ierr;
    DM            da;
    Mat           A;
    Vec           b,u,uexact;
    KSP           ksp;
    PetscReal     errnorm;
    DMDALocalInfo info;
    PetscLogDouble t1, t2;
    PetscMPIInt rank;

    
    ierr = PetscInitialize(&argc, &args, NULL, help); if (ierr) return ierr;
    PetscCall(PetscTime(&t1));
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    // change default 9x9 size using -da_grid_x M -da_grid_y N
    ierr = DMDACreate2d(PETSC_COMM_WORLD,  // MPI_Comm comm
                        DM_BOUNDARY_NONE,  // DMBoundaryType bx, type of ghost nodes at boundary
                        DM_BOUNDARY_NONE,  // DMBoundaryType by   /DM_BOUNDARY_NONE/DM_BOUNDARY_GHOSTED/DM_BOUNDARY_PERIODIC/
                        DMDA_STENCIL_STAR, // DMDAStencilType stype, /DMDA_STENCIL_BOX/DMDA_STENCIL_STAR/
                        9,                 // PetscInt M, global dimension in each direction of the array
                        9,                 // PetscInt N
                        PETSC_DECIDE,      // PetscInt m, number of process in each dimension
                        PETSC_DECIDE,      // PetscInt n
                        1,                 // PetscInt dof, number of degrees of freedom per node
                        1,                 // PetscInt s, stencil width, the number of grid points away from the center of the stencil
                        NULL,              // const PetscInt lx[], arrays containing the number of nodes in each process' portions of the grid, or NULL
                        NULL,              // const PetscInt ly[], if non-NULL, these must be of length m and n, respectivly, and the sum of lx[] and ly[] must be M and N respectively
                        &da);              // DM *da, the resulting DMDA object
    CHKERRQ(ierr);

    // create linear system matrix A
    ierr = DMSetFromOptions(da); CHKERRQ(ierr);
    ierr = DMSetUp(da); CHKERRQ(ierr);
    ierr = DMCreateMatrix(da,&A); CHKERRQ(ierr);
    ierr = MatSetFromOptions(A); CHKERRQ(ierr);

    // create RHS b, approx solution u, exact solution uexact
    ierr = DMCreateGlobalVector(da,&b); CHKERRQ(ierr);
    ierr = VecDuplicate(b,&u); CHKERRQ(ierr);
    ierr = VecDuplicate(b,&uexact); CHKERRQ(ierr);

    // fill vectors and assemble linear system
    ierr = formExact(da,uexact); CHKERRQ(ierr);
    ierr = formRHS(da,b); CHKERRQ(ierr);
    ierr = formMatrix(da,A); CHKERRQ(ierr);

    // create and solve the linear system
    ierr = KSPCreate(PETSC_COMM_WORLD,&ksp); CHKERRQ(ierr);
    ierr = KSPSetOperators(ksp,A,A); CHKERRQ(ierr);
    ierr = KSPSetFromOptions(ksp); CHKERRQ(ierr);
    ierr = KSPSolve(ksp,b,u); CHKERRQ(ierr);

    // report on grid and numerical error
    ierr = VecAXPY(u,-1.0,uexact); CHKERRQ(ierr);    // u <- u + (-1.0) uxact
    ierr = VecNorm(u,NORM_INFINITY,&errnorm); CHKERRQ(ierr);
    ierr = DMDAGetLocalInfo(da,&info);CHKERRQ(ierr);
    ierr = PetscPrintf(PETSC_COMM_WORLD, "on %d x %d grid:  error |u-uexact|_inf = %g\n", info.mx,info.my,errnorm); CHKERRQ(ierr);

    VecDestroy(&u);
    VecDestroy(&uexact);
    VecDestroy(&b);
    MatDestroy(&A);
    KSPDestroy(&ksp);
    DMDestroy(&da);
    PetscCall(PetscTime(&t2));
     ierr = PetscPrintf(PETSC_COMM_SELF, "Processor %d took %f CPU seconds\n", (int)rank, (t2-t1)); CHKERRQ(ierr);

    return PetscFinalize();
}
//ENDMAIN

//STARTMATRIX
PetscErrorCode formMatrix(DM da, Mat A) {
    PetscErrorCode ierr;
    DMDALocalInfo  info;
    MatStencil     row, col[5];
    PetscReal      hx, hy, v[5];
    PetscInt       i, j, ncols;

    ierr = DMDAGetLocalInfo(da,&info); CHKERRQ(ierr);
    hx = 1.0/(info.mx-1);  hy = 1.0/(info.my-1);
    for (j = info.ys; j < info.ys+info.ym; j++) {
        for (i = info.xs; i < info.xs+info.xm; i++) {
            row.j = j;           // row of A corresponding to (x_i,y_j)
            row.i = i;
            col[0].j = j;        // diagonal entry
            col[0].i = i;
            ncols = 1;
            if (i==0 || i==info.mx-1 || j==0 || j==info.my-1) {
                v[0] = 1.0;      // on boundary: trivial equation
            } else {
                v[0] = 2*(hy/hx + hx/hy); // interior: build a row
                if (i-1 > 0) {
                    col[ncols].j = j;    col[ncols].i = i-1;
                    v[ncols++] = -hy/hx;
                }
                if (i+1 < info.mx-1) {
                    col[ncols].j = j;    col[ncols].i = i+1;
                    v[ncols++] = -hy/hx;
                }
                if (j-1 > 0) {
                    col[ncols].j = j-1;  col[ncols].i = i;
                    v[ncols++] = -hx/hy;
                }
                if (j+1 < info.my-1) {
                    col[ncols].j = j+1;  col[ncols].i = i;
                    v[ncols++] = -hx/hy;
                }
            }
            ierr = MatSetValuesStencil(A,1,&row,ncols,col,v,INSERT_VALUES); CHKERRQ(ierr);
        }
    }
    ierr = MatAssemblyBegin(A,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
    ierr = MatAssemblyEnd(A,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
    return 0;
}
//ENDMATRIX

//STARTEXACT
PetscErrorCode formExact(DM da, Vec uexact) {
    PetscErrorCode ierr;
    PetscInt       i, j;
    PetscReal      hx, hy, x, y, **auexact;
    DMDALocalInfo  info;

    ierr = DMDAGetLocalInfo(da,&info); CHKERRQ(ierr);
    hx = 1.0/(info.mx-1);  hy = 1.0/(info.my-1);
    ierr = DMDAVecGetArray(da, uexact, &auexact);CHKERRQ(ierr);
    for (j = info.ys; j < info.ys+info.ym; j++) {
        y = j * hy;
        for (i = info.xs; i < info.xs+info.xm; i++) {
            x = i * hx;
            auexact[j][i] = x*x * (1.0 - x*x) * y*y * (y*y - 1.0);
        }
    }
    ierr = DMDAVecRestoreArray(da, uexact, &auexact);CHKERRQ(ierr);
    return 0;
}

PetscErrorCode formRHS(DM da, Vec b) {
    PetscErrorCode ierr;
    PetscInt       i, j;
    PetscReal      hx, hy, x, y, f, **ab;
    DMDALocalInfo  info;

    ierr = DMDAGetLocalInfo(da,&info); CHKERRQ(ierr);
    hx = 1.0/(info.mx-1);  hy = 1.0/(info.my-1);
    ierr = DMDAVecGetArray(da, b, &ab);CHKERRQ(ierr);
    for (j=info.ys; j<info.ys+info.ym; j++) {
        y = j * hy;
        for (i=info.xs; i<info.xs+info.xm; i++) {
            x = i * hx;
            if (i==0 || i==info.mx-1 || j==0 || j==info.my-1) {
                ab[j][i] = 0.0;  // on boundary: 1*u = 0
            } else {
                f = 2.0 * ( (1.0 - 6.0*x*x) * y*y * (1.0 - y*y)
                    + (1.0 - 6.0*y*y) * x*x * (1.0 - x*x) );
                ab[j][i] = hx * hy * f;
            }
        }
    }
    ierr = DMDAVecRestoreArray(da, b, &ab); CHKERRQ(ierr);
    return 0;
}
//ENDEXACT

