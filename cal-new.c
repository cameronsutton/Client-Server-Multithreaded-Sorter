#include <string.h>
#include <stdio.h>
#include <sys/unistd.h>
#include <stdint.h>
#include <pthread.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>

#define N 1024
int NUM_MERGER_LAYERS;

_Noreturn void* sort_thread(void* arg);
_Noreturn void* merge_thread(void* arg);
_Noreturn void* input_thread(void* arg);
int compare(const void* a, const void* b);

int childr, childw, M, A, D;
int num_layers = 1; // start at 1 as this is the base layer
const int one = 1, neg_one = -1;
pthread_mutex_t read_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t read_condition = PTHREAD_COND_INITIALIZER;
uint8_t read_signal = 1;

int main(int argc, char* argv[]) {
    int i, j, length;
    setbuf(stdout, 0);
    if (argc == 6) {
        childr = (int)strtol(argv[1], NULL, 10);
        childw = (int)strtol(argv[2], NULL, 10);
        M = (int)strtol(argv[3], NULL, 10);
        A = (int)strtol(argv[4], NULL, 10);
        D = (int)strtol(argv[5], NULL, 10);
    }
    else {
        fprintf(stderr, "\"cal\" process expected 6 arguments but received %d, Exiting.\n", argc);
        return -1;
    }
    NUM_MERGER_LAYERS = (int)log2(M);

    int** input_arrays = (int**) malloc(A * sizeof(int*));
    uint8_t* free_arrays = (uint8_t*) malloc(A * sizeof(int));

    pthread_t* input_threads = (pthread_t*) malloc(A * sizeof(pthread_t));
    pthread_mutex_t* input_mutexes = (pthread_mutex_t*) malloc(A * sizeof(pthread_mutex_t));
    pthread_cond_t* input_conditions = (pthread_cond_t*) malloc(A * sizeof(pthread_cond_t));
    uint8_t* input_signals = (uint8_t*) malloc(A * sizeof(uint8_t));

    // length M, one for each sorter thread
    pthread_t** sorter_threads = (pthread_t**) malloc(A * sizeof(pthread_t*));
    pthread_mutex_t** sorter_mutexes = (pthread_mutex_t**) malloc(A * sizeof(pthread_mutex_t*));
    pthread_cond_t** sorter_conditions = (pthread_cond_t**) malloc(A * sizeof(pthread_cond_t*));
    uint8_t** sorter_signals = (uint8_t**) malloc(A * sizeof(uint8_t*));

    // length 2M, one for each merger thread
    pthread_t** merger_threads = (pthread_t**) malloc(A * sizeof(pthread_t*));
    pthread_mutex_t** merger_mutexes = (pthread_mutex_t**) malloc(A * sizeof(pthread_mutex_t*));
    pthread_cond_t** merger_conditions = (pthread_cond_t**) malloc(A * sizeof(pthread_cond_t*));
    uint8_t** merger_signals = (uint8_t**) malloc(A * sizeof(uint8_t*));

    // syncs the layers for layer-by-layer printing if D = 1
    // merger threads adds 1 to signal then waits until signal == -1
    // input threads wait until signal = 2^(layer), sets signal to -1
    pthread_mutex_t** layer_sync_mutexes = (pthread_mutex_t**) malloc(A * sizeof(pthread_mutex_t*));
    pthread_cond_t** layer_sync_conditions = (pthread_cond_t**) malloc(A * sizeof(pthread_cond_t*));
    uint16_t** layer_sync_signals = (uint16_t**) malloc(A * sizeof(int16_t*));

    // used to tell input thread to print the layer and reactivate all the mergers if D == 1
    pthread_mutex_t** layer_done_mutexes = (pthread_mutex_t**) malloc(A * sizeof(pthread_mutex_t*));
    pthread_cond_t** layer_done_conditions = (pthread_cond_t**) malloc(A * sizeof(pthread_cond_t*));
    uint8_t** layer_done_signals = (uint8_t**) malloc(A * sizeof(int8_t*));

    // memory initialization
    for (i = 0; i < A; i++) {
        input_arrays[i] = (int*) malloc(N * sizeof(int));
        free_arrays[i] = 1; // every array is free to begin with

        input_threads[i] = PTHREAD_CREATE_JOINABLE;
        input_mutexes[i] = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
        input_conditions[i] = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
        input_signals[i] = 0;

        sorter_threads[i] = (pthread_t*) malloc(M * sizeof(pthread_t));
        sorter_mutexes[i] = (pthread_mutex_t*) malloc(M * sizeof(pthread_mutex_t));
        sorter_conditions[i] = (pthread_cond_t*) malloc(M * sizeof(pthread_cond_t));
        sorter_signals[i] = (uint8_t*) malloc(M * sizeof(uint8_t));

        merger_threads[i] = (pthread_t*) malloc(M*2 * sizeof(pthread_t));
        merger_mutexes[i] = (pthread_mutex_t*) malloc(M * 2 * sizeof(pthread_mutex_t));
        merger_conditions[i] = (pthread_cond_t*) malloc(M * 2 * sizeof(pthread_cond_t));
        merger_signals[i] = (uint8_t*) malloc(M * 2 * sizeof(uint8_t));

        layer_sync_mutexes[i] = (pthread_mutex_t*) malloc(NUM_MERGER_LAYERS * sizeof(pthread_mutex_t));
        layer_sync_conditions[i] = (pthread_cond_t*) malloc(NUM_MERGER_LAYERS * sizeof(pthread_cond_t));
        layer_sync_signals[i] = (uint16_t*) malloc(NUM_MERGER_LAYERS * sizeof(uint16_t));

        layer_done_mutexes[i] = (pthread_mutex_t*) malloc(NUM_MERGER_LAYERS * sizeof(pthread_mutex_t));
        layer_done_conditions[i] = (pthread_cond_t*) malloc(NUM_MERGER_LAYERS * sizeof(pthread_cond_t));
        layer_done_signals[i] = (uint8_t*) malloc(NUM_MERGER_LAYERS * sizeof(uint8_t));


        for (j = 0; j < N; j++) input_arrays[i][j] = INT_MAX;

        for (j = 0; j < M; j++) {
            sorter_threads[i][j] = (pthread_t) PTHREAD_CREATE_JOINABLE;
            sorter_mutexes[i][j] = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
            sorter_conditions[i][j] = (pthread_cond_t) PTHREAD_COND_INITIALIZER;
            sorter_signals[i][j] = 0;
        }

        for (j = 0; j < M*2; j++) {
            merger_threads[i][j] = (pthread_t) PTHREAD_CREATE_JOINABLE;
            merger_mutexes[i][j] = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
            merger_conditions[i][j] = (pthread_cond_t) PTHREAD_COND_INITIALIZER;
            merger_signals[i][j] = 0;
        }

        for (j = 0; j < NUM_MERGER_LAYERS; j++) {
            layer_sync_mutexes[i][j] = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
            layer_sync_conditions[i][j] = (pthread_cond_t) PTHREAD_COND_INITIALIZER;
            layer_sync_signals[i][j] = 0;
            layer_done_mutexes[i][j] = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
            layer_done_conditions[i][j] = (pthread_cond_t) PTHREAD_COND_INITIALIZER;
            layer_done_signals[i][j] = 0;
        }
    }

    // thread creation
    uint64_t args[19];
    for (i = 0; i < A; i++) {
        args[0] = (uint64_t) input_arrays[i];

        // input thread
        args[1] = (uint64_t) &length;
        args[2] = (uint64_t) &input_mutexes[i];
        args[3] = (uint64_t) &input_conditions[i];
        args[4] = (uint64_t) &input_signals[i];
        args[5] = (uint64_t) sorter_mutexes[i];
        args[6] = (uint64_t) sorter_conditions[i];
        args[7] = (uint64_t) sorter_signals[i];
        args[8] = (uint64_t) &merger_mutexes[i][2*M-2];
        args[9] = (uint64_t) &merger_conditions[i][2*M-2];
        args[10] = (uint64_t) &merger_signals[i][2*M-2];
        args[11] = (uint64_t) &free_arrays[i];
        args[12] = (uint64_t) layer_sync_mutexes[i];
        args[13] = (uint64_t) layer_sync_conditions[i];
        args[14] = (uint64_t) layer_sync_signals[i];
        args[15] = (uint64_t) layer_done_mutexes[i];
        args[16] = (uint64_t) layer_done_conditions[i];
        args[17] = (uint64_t) layer_done_signals[i];
        pthread_create(&input_threads[i], NULL, input_thread, args);
        usleep(1000);   // let thread initialize before argument array is overwritten

        // sorter threads
        for (j = 0; j < M; j++) {
            // indexes
            args[1] = j * N / M;
            args[2] = (j + 1) * N / M;

            // sorter start signal
            args[3] = (uint64_t) &sorter_mutexes[i][j];
            args[4] = (uint64_t) &sorter_conditions[i][j];
            args[5] = (uint64_t) &sorter_signals[i][j];

            // merging ready signal
            args[6] = (uint64_t) &merger_mutexes[i][j];
            args[7] = (uint64_t) &merger_conditions[i][j];
            args[8] = (uint64_t) &merger_signals[i][j];

            pthread_create(&sorter_threads[i][j], NULL, sort_thread, args);
            usleep(1000);   // let thread initialize before argument array is overwritten
        }

        // merger threads
        int layer = 0;
        int layer_size = M / 2;
        int layer_index = M;
        while (layer_size > 0) {
            num_layers++;
            for (j = 0; j < layer_size; j++) {
                // indexes
                args[1] = j * N / layer_size;
                args[2] = (j + 1) * N / layer_size;

                // left-side merge start signal
                args[3] = (uint64_t) &merger_mutexes[i][layer_index - 2 * layer_size + 2 * j];
                args[4] = (uint64_t) &merger_conditions[i][layer_index - 2 * layer_size + 2 * j];
                args[5] = (uint64_t) &merger_signals[i][layer_index - 2 * layer_size + 2 * j];

                // right-side merge start signal
                args[6] = (uint64_t) &merger_mutexes[i][layer_index - 2 * layer_size + 2 * j + 1];
                args[7] = (uint64_t) &merger_conditions[i][layer_index - 2 * layer_size + 2 * j + 1];
                args[8] = (uint64_t) &merger_signals[i][layer_index - 2 * layer_size + 2 * j + 1];

                // merging finish signal
                args[9] = (uint64_t) &merger_mutexes[i][layer_index + j];
                args[10] = (uint64_t) &merger_conditions[i][layer_index + j];
                args[11] = (uint64_t) &merger_signals[i][layer_index + j];

                // layer-by-layer synchronization counter
                args[12] = (uint64_t) &layer_sync_mutexes[i][layer];
                args[13] = (uint64_t) &layer_sync_conditions[i][layer];
                args[14] = (uint64_t) &layer_sync_signals[i][layer];

                args[15] = (uint64_t) layer_size;

                // layer-by-layer continuation signal
                args[16] = (uint64_t) &layer_done_mutexes[i][layer];
                args[17] = (uint64_t) &layer_done_conditions[i][layer];
                args[18] = (uint64_t) &layer_done_signals[i][layer];

                pthread_create(&merger_threads[i][j], NULL, merge_thread, args);
                usleep(1000);   // let thread initialize before argument array is overwritten
            }
            layer_index += layer_size;
            layer_size >>= 1;
            layer++;
        }
    }

    while (1) {
        // ensure there isn't an input thread currently reading from pipe
        pthread_mutex_lock(&read_mutex);
        while (!read_signal) {
            pthread_cond_wait(&read_condition, &read_mutex);
        }
        while (read(childr, &length, sizeof(int)) == 0);
        read_signal = 0;
        pthread_cond_signal(&read_condition);
        pthread_mutex_unlock(&read_mutex);

        write(childw, &neg_one, sizeof(int));
        if (length == -1) { //shutdown signal
            // kill threads
            for (i = 0; i < A; i++) {
                pthread_cancel(input_threads[i]);
                for (j = 0; i < M; i++) {
                    pthread_cancel(sorter_threads[i][j]);
                }
                for (j = 0; i < 2 * M - 2; i++) {
                    pthread_cancel(merger_threads[i][j]);
                }
            }
            exit(EXIT_SUCCESS);
        }

        // find an unused array
        int array = -1;
            for (i = 0; i < A; i++)
                if (free_arrays[i] == 1)
                    array = i;
        free_arrays[array] = 0;

        // signal input thread to go
        pthread_mutex_lock(&input_mutexes[array]);
        input_signals[array] = 1;
        pthread_cond_signal(&input_conditions[array]);
        pthread_mutex_unlock(&input_mutexes[array]);
    }
}

// arg[0] = input array address
// arg[1] = length address
// arg[2] = input mutex address
// arg[3] = input cond address
// arg[4] = input signal address
// arg[5] = sorter mutex array
// arg[6] = sorter cond array
// arg[7] = sorter signal array
// arg[8] = merger mutex address
// arg[9] = merger cond address
// arg[10] = merger signal address
// arg[11] = free array flag pointer
// arg[12] = layer sync mutex array
// arg[13] = layer sync cond array
// arg[14] = layer sync signal array
// arg[15] = layer done mutex array
// arg[16] = layer done cond array
// arg[17] = layer done signal array
_Noreturn void* input_thread(void* arg) {
    uint64_t* args = (uint64_t*)arg;

    int* input_array = (int*)args[0];
    int* length_ptr = (int*)args[1];

    // pointer to specific value
    pthread_mutex_t* input_mutex_ptr = (pthread_mutex_t*)args[2];
    pthread_cond_t* input_condition_ptr = (pthread_cond_t*)args[3];
    uint8_t* input_signal_ptr = (uint8_t*)args[4];

    // array of values
    pthread_mutex_t* sorter_mutex_array = (pthread_mutex_t*)args[5];
    pthread_cond_t* sorter_cond_array = (pthread_cond_t*)args[6];
    uint8_t* sorter_signal_array = (uint8_t*)args[7];

    // pointer to specific value
    pthread_mutex_t* finish_mutex_ptr = (pthread_mutex_t*)args[8];
    pthread_cond_t* finish_condition_ptr = (pthread_cond_t*)args[9];
    uint8_t* finish_signal_ptr = (uint8_t*)args[10];

    uint8_t* free_array_ptr = (uint8_t*)args[11];

    // array of values
    pthread_mutex_t* layer_sync_mutex_array = (pthread_mutex_t*)args[12];
    pthread_cond_t* layer_sync_cond_array = (pthread_cond_t*)args[13];
    int16_t* layer_sync_signal_array = (int16_t*)args[14];

    // array of values
    pthread_mutex_t* layer_done_mutex_array = (pthread_mutex_t*)args[15];
    pthread_cond_t* layer_done_cond_array = (pthread_cond_t*)args[16];
    int8_t* layer_done_signal_array = (int8_t*)args[17];

    int i, j, layer, length, CID, filelen;
    char filename[80];
    while (1) {
        bzero(filename, sizeof(char)*80);
        pthread_mutex_lock(input_mutex_ptr);
        while (!(*input_signal_ptr)) {
            pthread_cond_wait(input_condition_ptr, input_mutex_ptr);
        }

        length = *length_ptr, layer = 1;

        // read data
        pthread_mutex_lock(&read_mutex);
        while (read_signal) {
            pthread_cond_wait(&read_condition, &read_mutex);
        }
        read(childr, &CID, sizeof(int));
        read(childr, &filelen, sizeof(int));
        read(childr, filename, sizeof(char)*filelen);
        read(childr, input_array, sizeof(int) * length);

        read_signal = 1;
        pthread_cond_signal(&read_condition);
        pthread_mutex_unlock(&read_mutex);

        if (D) {
            printf("\n==================== CID=%d, file=%s, N=%d, L=%d", CID, filename, length, layer);
            for (j = 0; j < length; j++) {
                if (j % 10 == 0) printf("\n");
                printf("%d ", input_array[j]);
            }
            printf("\n");
            layer++;
        }
        // signal sorter threads to start sorting
        for (i = 0; i < M; i++) {
            pthread_mutex_lock(&sorter_mutex_array[i]);
            sorter_signal_array[i] = 1;
            pthread_cond_signal(&sorter_cond_array[i]);
            pthread_mutex_unlock(&sorter_mutex_array[i]);
        }

        if (D) {
            int gap_between_mergers = N/M * 2;
            int used_mergers_in_layer = length / gap_between_mergers;
            for (i = 0; i < NUM_MERGER_LAYERS; i++) {
                pthread_mutex_lock(&layer_done_mutex_array[i]);
                while (!layer_done_signal_array[i]) {
                    pthread_cond_wait(&layer_done_cond_array[i], &layer_done_mutex_array[i]);
                }
                layer_done_signal_array[i] = 0;
                pthread_mutex_unlock(&layer_done_mutex_array[i]);

                if (used_mergers_in_layer > 0) {
                    printf("\n==================== CID=%d, file=%s, N=%d, L=%d", CID, filename, length, layer);
                    for (j = 0; j < length; j++) {
                        if (j % 10 == 0) printf("\n");
                        printf("%d ", input_array[j]);
                    }
                    printf("\n");
                    used_mergers_in_layer >>= 1;
                    layer++;
                }

                pthread_mutex_lock(&layer_sync_mutex_array[i]);
                layer_sync_signal_array[i] = 0;
                pthread_cond_broadcast(&layer_sync_cond_array[i]);
                pthread_mutex_unlock(&layer_sync_mutex_array[i]);
            }
        }
        //wait for sorting to finish
        pthread_mutex_lock(finish_mutex_ptr);
        while (!(*finish_signal_ptr)) {
            pthread_cond_wait(finish_condition_ptr, finish_mutex_ptr);
        }

        if (D) {
            printf("\n==================== CID=%d, file=%s, N=%d, L=%d", CID, filename, length, layer);
        } else {
            printf("\n====================");
        }
        for (i = 0; i < length; i++) {
            if (i % 10 == 0) printf("\n");
            printf("%d ", input_array[i]);
            input_array[i] = INT_MAX; // clear array after use
        }
        printf("\n");

        *finish_signal_ptr = 0;
        pthread_mutex_unlock(finish_mutex_ptr);

        *input_signal_ptr = 0;
        *free_array_ptr = 1;
        pthread_mutex_unlock(input_mutex_ptr);
    }
}

// arg[0] = input array address
// arg[1] = start index
// arg[2] = end index
// arg[3] = left mutex address
// arg[4] = left cond address
// arg[5] = left signal address
// arg[6] = right mutex address
// arg[7] = right cond address
// arg[8] = right signal address
// arg[9] = finish mutex address
// arg[10] = finish cond address
// arg[11] = finish signal address
// arg[12] = layer sync mutex address
// arg[13] = layer sync cond address
// arg[14] = layer sync signal address
// arg[15] = merger threads in layer
// arg[16] = layer done mutex address
// arg[17] = layer done cond address
// arg[18] = layer done signal address
_Noreturn void* merge_thread(void* arg) {
    uint64_t* args = (uint64_t*)arg;

    int* input_array = (int*)args[0];
    const int start = (int)args[1];
    const int end = (int)args[2];

    pthread_mutex_t* left_mutex_ptr = (pthread_mutex_t*)args[3];
    pthread_cond_t* left_condition_ptr = (pthread_cond_t*)args[4];
    uint8_t* left_signal_ptr = (uint8_t*)args[5];

    pthread_mutex_t* right_mutex_ptr = (pthread_mutex_t*)args[6];
    pthread_cond_t* right_condition_ptr = (pthread_cond_t*)args[7];
    uint8_t* right_signal_ptr = (uint8_t*)args[8];

    pthread_mutex_t* finish_mutex_ptr = (pthread_mutex_t*)args[9];
    pthread_cond_t* finish_condition_ptr = (pthread_cond_t*)args[10];
    uint8_t* finish_signal_ptr = (uint8_t*)args[11];

    pthread_mutex_t* layer_sync_mutex_ptr = (pthread_mutex_t*)args[12];
    pthread_cond_t* layer_sync_condition_ptr = (pthread_cond_t*)args[13];
    uint16_t* layer_sync_signal_ptr = (uint16_t*)args[14];

    int num_mergers_in_layer = (int)args[15];

    pthread_mutex_t* layer_done_mutex_ptr = (pthread_mutex_t*)args[16];
    pthread_cond_t* layer_done_condition_ptr = (pthread_cond_t*)args[17];
    uint8_t* layer_done_signal_ptr = (uint8_t*)args[18];

    while (1) {
        pthread_mutex_lock(left_mutex_ptr);
        while (!(*left_signal_ptr)) {
            pthread_cond_wait(left_condition_ptr, left_mutex_ptr);
        }

        pthread_mutex_lock(right_mutex_ptr);
        while (!(*right_signal_ptr)) {
            pthread_cond_wait(right_condition_ptr, right_mutex_ptr);
        }


        if (D) {
            pthread_mutex_lock(layer_sync_mutex_ptr);
            (*layer_sync_signal_ptr)++;
            if ((*layer_sync_signal_ptr) == num_mergers_in_layer) {
                pthread_mutex_lock(layer_done_mutex_ptr);
                (*layer_done_signal_ptr) = 1;
                pthread_cond_signal(layer_done_condition_ptr);  // alert the input thread
                pthread_mutex_unlock(layer_done_mutex_ptr);
            }

            while (*layer_sync_signal_ptr != 0) {
                pthread_cond_wait(layer_sync_condition_ptr, layer_sync_mutex_ptr);
            }
            pthread_mutex_unlock(layer_sync_mutex_ptr);
        }


        // merge right side into left side
        int i, j, temp;
        for (i = start + (end - start)/2; i < end; i++) {
            for (j = i; j > start; j--) {
                if (input_array[j] < input_array[j-1]) {
                    temp = input_array[j-1];
                    input_array[j-1] = input_array[j];
                    input_array[j] = temp;
                } else {
                    break;
                }
            }
        }

        *left_signal_ptr = 0;
        *right_signal_ptr = 0;
        pthread_mutex_unlock(left_mutex_ptr);
        pthread_mutex_unlock(right_mutex_ptr);

        // alert next layer to begin merging
        pthread_mutex_lock(finish_mutex_ptr);
        *finish_signal_ptr = 1;
        pthread_cond_signal(finish_condition_ptr);
        pthread_mutex_unlock(finish_mutex_ptr);
    }
}

// arg[0] = input array address
// arg[1] = start index
// arg[2] = end index
// arg[3] = start mutex address
// arg[4] = start cond address
// arg[5] = start signal address
// arg[6] = finish mutex address
// arg[7] = finish cond address
// arg[8] = finish signal address
_Noreturn void* sort_thread(void* arg) {
    uint64_t* args = (uint64_t*)arg;

    int* input_array = (int*)args[0];
    const int start = (int)args[1];
    const int end = (int)args[2];

    pthread_mutex_t* start_mutex_ptr = (pthread_mutex_t*)args[3];
    pthread_cond_t* start_condition_ptr = (pthread_cond_t*)args[4];
    uint8_t* start_signal_ptr = (uint8_t*)args[5];

    pthread_mutex_t* finish_mutex_ptr = (pthread_mutex_t*)args[6];
    pthread_cond_t* finish_condition_ptr = (pthread_cond_t*)args[7];
    uint8_t* finish_signal_ptr = (uint8_t*)args[8];

    while (1) {
        pthread_mutex_lock(start_mutex_ptr);
        while (!(*start_signal_ptr)) {
            pthread_cond_wait(start_condition_ptr, start_mutex_ptr);
        }
        qsort(&(input_array[start]), end - start, sizeof(int), compare);
        *start_signal_ptr = 0;  // reset to stop sorter from running again
        pthread_mutex_unlock(start_mutex_ptr);

        // alert merger that it is done sorting
        pthread_mutex_lock(finish_mutex_ptr);
        *finish_signal_ptr = 1;
        pthread_cond_signal(finish_condition_ptr);
        pthread_mutex_unlock(finish_mutex_ptr);
    }
}

int compare(const void* a, const void* b) {
    return *(int*)a - *(int*)b;
}