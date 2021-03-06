#include "Jacobi.h"

void grid_print(int layer_index, double *grid)
{
    for (int j = 0; j < NY; j++)
    {
        for (int k = 0; k < NZ; k++)
        {
            printf("%.3lf ", grid[I(layer_index, j, k)]);
        }
        printf("\n");
    }
    printf("\n");
}

bool is_boundary_cell(int index, int bound)
{
    return index == 0 || index == bound - 1;
}


void grid_init(double *grid, double *previous_grid, int pr_cells_count, int pr_cells_shift)
{
    for (int i = 1; i < pr_cells_count + 1; i++)
    {
        for (int j = 0; j < NY; j++)
        {
            for (int k = 0; k < NZ; k++)
            {
                grid[I(i, j, k)] = PHI0;
                previous_grid[I(i, j, k)] = PHI0;

                int actual_i = i + pr_cells_shift;

                if (is_boundary_cell(actual_i, NX) || is_boundary_cell(j, NY) || is_boundary_cell(k, NZ))
                {
                    grid[I(i, j, k)] = phi(actual_i, j, k);
                    previous_grid[I(i, j, k)] = phi(actual_i, j, k);
                }
            }
        }
    }
}

void update_grid_center(double *grid, double *previous_grid, int pr_cells_count, int pr_cells_shift, double *pr_diff)
{
    int center = (pr_cells_count + 1) / 2;

    update_grid_cell(center, grid, previous_grid, pr_cells_shift, pr_diff);

    for (int j = 1; j < (pr_cells_count + 1) / 2; j++)
    {
        update_grid_cell(center - j, grid, previous_grid, pr_cells_shift, pr_diff);
        update_grid_cell(center + j, grid, previous_grid, pr_cells_shift, pr_diff);
    }

    if (pr_cells_count % 2 == 0)
    {
        update_grid_cell(pr_cells_count, grid, previous_grid, pr_cells_shift, pr_diff);
    }
}

void update_grid_bound(double *grid, double *previous_grid, int pr_cells_count, int pr_cells_shift, double *pr_diff)
{
    update_grid_cell(1, grid, previous_grid, pr_cells_shift, pr_diff);
    update_grid_cell(pr_cells_count, grid, previous_grid, pr_cells_shift, pr_diff);
}

void send_grid_bound(double *grid, int pr_rank, int comm_size, int pr_cells_count, MPI_Request *request_prev,
                     MPI_Request *request_next)
{
    int layer_size = NY * NZ;
    if (pr_rank != 0)
    {
        MPI_Isend(grid + layer_size, layer_size, MPI_DOUBLE, pr_rank - 1, 0, MPI_COMM_WORLD, &request_next[0]);
        MPI_Irecv(grid, layer_size, MPI_DOUBLE, pr_rank - 1, 0, MPI_COMM_WORLD, &request_next[1]);
    }
    if (pr_rank != comm_size - 1)
    {
        MPI_Isend(grid + (pr_cells_count) * layer_size, layer_size, MPI_DOUBLE, pr_rank + 1, 0, MPI_COMM_WORLD,
                  &request_prev[0]);
        MPI_Irecv(grid + (pr_cells_count + 1) * layer_size, layer_size, MPI_DOUBLE, pr_rank + 1, 0, MPI_COMM_WORLD,
                  &request_prev[1]);
    }
}

void recieve_grid_bound(int pr_rank, int comm_size, MPI_Request *request_prev, MPI_Request *request_next)
{
    if (pr_rank != 0)
    {
        MPI_Wait(&request_next[0], MPI_STATUS_IGNORE);
        MPI_Wait(&request_next[1], MPI_STATUS_IGNORE);
    }
    if (pr_rank != comm_size - 1)
    {
        MPI_Wait(&request_prev[0], MPI_STATUS_IGNORE);
        MPI_Wait(&request_prev[1], MPI_STATUS_IGNORE);
    }
}

double iteration_func(int i, int j, int k, double *grid, int pr_cells_shift)
{
    return ((1.0 / (2.0 / (hx * hx) + 2.0 / (hy * hy) + 2.0 / (hz * hz) + A)) *
            ((grid[I(i + 1, j, k)] + grid[I(i - 1, j, k)]) / (hx * hx) +
             (grid[I(i, j + 1, k)] + grid[I(i, j - 1, k)]) / (hy * hy) +
             (grid[I(i, j, k + 1)] + grid[I(i, j, k - 1)]) / (hz * hz) -
             ro(i + pr_cells_shift, j, k)));
}

void update_previous_grid(int index, double *current_value, double *prev_value)
{
    for (int j = 1; j < NY - 1; j++)
    {
        for (int k = 1; k < NZ - 1; k++)
        {
            prev_value[I(index, j, k)] = current_value[I(index, j, k)];
        }
    }
}

void update_grid_cell(int index, double *grid, double *previous_grid, int pr_cells_shift, double *pr_diff)
{
    int actual_index = index + pr_cells_shift;
    if (!is_boundary_cell(actual_index, NX))
    {
        for (int j = 1; j < NY - 1; j++)
        {
            for (int k = 1; k < NZ - 1; k++)
            {
                grid[I(index, j, k)] = iteration_func(index, j, k, previous_grid, pr_cells_shift);

                double cur_diff = fabs(grid[I(index, j, k)] - previous_grid[I(index, j, k)]);

                if (*pr_diff < cur_diff)
                {
                    *pr_diff = cur_diff;
                }
            }
        }
    }

    update_previous_grid(index, grid, previous_grid);
}

int jacobi_method(double *grid, int pr_cells_count, int pr_cells_shift)
{
    double *previous_grid = (double *) malloc(sizeof(double) * (pr_cells_count + 2) * NY * NZ);
    grid_init(grid, previous_grid, pr_cells_count, pr_cells_shift);

    int iteration_count = 0;
    double diff = 0;

    int pr_rank, comm_size;
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &pr_rank);

    MPI_Request request_prev[2];
    MPI_Request request_next[2];

    do
    {
        double pr_diff = 0;

        send_grid_bound(previous_grid, pr_rank, comm_size, pr_cells_count, request_prev, request_next);
        update_grid_center(grid, previous_grid, pr_cells_count, pr_cells_shift, &pr_diff);
        recieve_grid_bound(pr_rank, comm_size, request_prev, request_next);
        update_grid_bound(grid, previous_grid, pr_cells_count, pr_cells_shift, &pr_diff);
        MPI_Allreduce(&pr_diff, &diff, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
        iteration_count++;

    } while (diff >= E);

    free(previous_grid);

    return iteration_count;
}

double get_observational_error(double *grid, int pr_cells_count, int pr_cells_shift)
{
    double pr_observational_error = 0;

    for (int i = 1; i < pr_cells_count + 1; i++)
    {
        for (int j = 0; j < NY; j++)
        {
            for (int k = 0; k < NZ; k++)
            {
                int actual_i = i + pr_cells_shift;
                double temp = fabs(grid[I(i, j, k)] - phi(actual_i, j, k));
                if (temp > pr_observational_error)
                {
                    pr_observational_error = temp;
                }
            }
        }
    }

    printf("%lf\n", pr_observational_error);

    double observational_error;
    MPI_Reduce(&pr_observational_error, &observational_error, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    return observational_error;
}
