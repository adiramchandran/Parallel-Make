/**
* Parallel Make Lab
* CS 241 - Spring 2018
*/


#include "format.h"
#include "graph.h"
#include <semaphore.h>
#include <pthread.h>
#include "parmake.h"
#include "parser.h"
#include "queue.h"
#include "dictionary.h"
#include "vector.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

typedef struct task{
    rule_t *rule;
    int exec_finish;
} task;

int num_roots = 0;
queue* Q;
graph* G;
dictionary* all_tasks;
dictionary* v_dict;
pthread_mutex_t glob_mtx;
sem_t sem;

/********  HELPERS  ********/

//setup and create task
task* create_task(void* v);

//destroy task
void destroy_task(task* t);

//set all vertices to unexplored
void vertex_reset();

//cycle detection on a single veretx; if there is a cross edge, return 1; otherwise, 0
int cycle_detection(void* v);

//push leaf node tasks to queue
void task_setup(void* v, void* parent);

//make sure children exited cleanly
int check_children_status(void* v);

//check if a rule is a file on disk; return 1 for no, 0 for yes
int check_no_file_disk(void* v, struct stat orig_buf);

//thread func; execute commands here
void* thread_todo(void* tid);

/********   DONE   ********/

task* create_task(void* v){
    task* t = malloc(sizeof(task));
    rule_t *r = (rule_t *)graph_get_vertex_value(G, v);
    t->rule = r;
    t->exec_finish = 0;
    return t;
}

void destroy_task(task* t){
    free(t);
}

void vertex_reset(){
    long zero = 0;
    vector* vertices = graph_vertices(G);
    VECTOR_FOR_EACH(vertices, v, {
        dictionary_set(v_dict, v, (void*)&zero); // all vertices unexplored
    });
    vector_destroy(vertices);
}

int cycle_detection(void* v){
    long one = 1;
    if (!dictionary_contains(v_dict, v))
        printf("line 88: no %s in dict\n", (char*)v);
    if (*((long*)dictionary_get(v_dict, v)) == 0){ // if node has not been visited
        dictionary_set(v_dict, v, (void*)&one); // set visited
        vector* neighbors = graph_neighbors(G, v);
        VECTOR_FOR_EACH(neighbors, adj, {
            //printf("%s val: %ld\n", (char*)adj, *((long*)dictionary_get(v_dict, adj)));
            if (!dictionary_contains(v_dict, adj))
                printf("line 95: no %s in dict\n", (char*)adj);
            if (*((long*)dictionary_get(v_dict, adj)) == 0){
                int ret = cycle_detection(adj);
                vector_destroy(neighbors);
                return ret;
            }
            else{
                // repeated edge --> cycle --> return 1
                vector_destroy(neighbors);
                return 1;
            }
        });
        vector_destroy(neighbors);
    }
    return 0;
}

int check_no_file_disk(void* v, struct stat orig_buf){
    rule_t *rule = (rule_t *)graph_get_vertex_value(G, v);
    // if rule is not file on disk, exec_flag is 1
    struct stat buf;
    int out = stat(rule->target, &buf);
    if (out < 0)
        return 1;
    //only other way exec_flag can be 1 is if this is a newer file
    if (buf.st_mtime > orig_buf.st_mtime)
        return 1;
    return 0;
}

void task_setup(void* v, void* parent){
    task* t = NULL;
    // if task has already been created, exit
    if (!dictionary_contains(v_dict, v))
        printf("line 129: no %s in dict\n", (char*)v);
    if (*((long*)dictionary_get(v_dict, v)) == 1)
        return;
    else{ // create task
        t = create_task(v);
        dictionary_set(all_tasks, v, (void*)&t);
        // set visited
        long one = 1;
        dictionary_set(v_dict, v, (void*)&one);
    }
    int leaf = 0;

    if (parent == NULL)
        num_roots++;

    vector* neighbors = graph_neighbors(G, v);
    if (vector_size(neighbors) == 0)
        leaf = 1;
    VECTOR_FOR_EACH(neighbors, adj, {
        task_setup(adj, v);
    });
    vector_destroy(neighbors);

    // deal w leaf node
    if (leaf){
        queue_push(Q, (void*)t);
        //printf("task %s was just pushed\n", t->rule->target);
    }
}

int check_children_status(void* v){
    vector* neighbors = graph_neighbors(G, v);
    VECTOR_FOR_EACH(neighbors, child, {
        if (!dictionary_contains(all_tasks, child))
            printf("line 163: no %s in tasks dict\n", (char*)child);
        task* curr = *((task**)dictionary_get(all_tasks, child));
        if (curr->exec_finish != 1){
            vector_destroy(neighbors);
            return 0;
        }
    });
    vector_destroy(neighbors);
    return 1;
}

void* thread_todo(void* tid){
    //printf("tid %d: entered todo\n", *((int*)tid));
    while(1){
        task* curr = (task*)queue_pull(Q);
        if (!curr){
            //printf("tid %d: pulled task is null\n", *((int*)tid));
            break;
        }
        if (curr->exec_finish != 0)
            continue;
        //printf("tid %d: working on task %s\n", *((int*)tid), curr->rule->target);
        int exec_flag = 0;
        // get stat for current task
        struct stat orig_buf;
        int out = stat(curr->rule->target, &orig_buf);
        if (out == -1)
            exec_flag = 1;

        //if exec_flag is still 0, check for dependencies being newer or not on disk
        if (!exec_flag){
            vector* neighbors = graph_neighbors(G, curr->rule->target);
            VECTOR_FOR_EACH(neighbors, adj, {
                if (check_no_file_disk(adj, orig_buf) == 1){
                    exec_flag = 1;
                    break;
                }
            });
            vector_destroy(neighbors);
        }

        pthread_mutex_lock(&glob_mtx);
        // make sure all children have succeeded
        int children_success = check_children_status((void*)curr->rule->target);
        pthread_mutex_unlock(&glob_mtx);

        //if exec_flag is 1, execute cmds, set exec_finish to 1
        //printf("tid %d: exec_flag val for %s is %d\n", *((int*)tid), curr->rule->target, exec_flag);
        if (exec_flag && children_success){
            VECTOR_FOR_EACH(curr->rule->commands, cmd, {
                int out = system(cmd);
                //printf("out val: %d\n", out);
                if (out != 0){
                    curr->exec_finish = 2; // finished; failed
                    break;
                }
            });
            if (curr->exec_finish != 2)
                curr->exec_finish = 1;
        }

        //if one or more of the children failed, set exec_finish to 2
        if (!children_success)
            curr->exec_finish = 2;
        //if the exec_flag is still 0, then the rule and its dependencies have been executed
        if (!exec_flag)
            curr->exec_finish = 1;

        pthread_mutex_lock(&glob_mtx);

        /* walk through parent vector
         * make sure all neighbors of each have finished
         */
        vector* parents = graph_antineighbors(G, curr->rule->target);
        if (strcmp( (char*)vector_get(parents, 0), "" ) != 0){
            VECTOR_FOR_EACH(parents, p, {
                //printf("tid %d: checking parent %s\n", *((int*)tid), (char*)p);
                int all_finish = 1;
                vector* neighbors = graph_neighbors(G, p);
                VECTOR_FOR_EACH(neighbors, child, {
                    if (!dictionary_contains(all_tasks, child))
                        continue;
                        //printf("line 244: no %s in tasks dict\n", (char*)child);
                    task* t = *((task**)dictionary_get(all_tasks, child));
                    //printf("tid %d: checking child %s\n", *((int*)tid), t->rule->target);
                    if (t->exec_finish == 0){
                        //printf("tid %d: child %s of %s is not finished\n", *((int*)tid), (char*)child, (char*)p);
                        all_finish = 0;
                        break;
                    }
                });
                vector_destroy(neighbors);

                // if all have finished, push parent to queue
                if (all_finish){
                    if (!dictionary_contains(all_tasks, p))
                        continue;
                        //printf("line 258: no %s in tasks dict\n", (char*)p);
                    task* parent = *((task**)dictionary_get(all_tasks, p));
                    //if(!parent)
                        //printf("tid %d: task pulled from dict is null\n", *((int*)tid));
                    //else
                        //printf("tid %d: task %s will be pushed\n", *((int*)tid), parent->rule->target);
                    queue_push(Q, (void*)parent);
                    //printf("tid %d: pushed parent %s of %s\n", *((int*)tid), parent->rule->target, curr->rule->target);
                }
            });
        }

        pthread_mutex_unlock(&glob_mtx);

        // if this is a root node (no parents), sem post
        if (strcmp( (char*)vector_get(parents, 0), "" ) == 0){ // sentinel
            //printf("tid %d: sem post called\n", *((int*)tid));
            sem_post(&sem);
        }
        vector_destroy(parents);
    }
    free(tid);
    return NULL;
}

int parmake(char *makefile, size_t num_threads, char **targets){
    pthread_t *threads = malloc(sizeof(pthread_t) * num_threads);
    // get graph with all rules and dependencies
    G = parser_parse_makefile(makefile, targets);
    Q = queue_create(-1);
    pthread_mutex_init(&glob_mtx, NULL);
    v_dict = string_to_long_dictionary_create();
    all_tasks = string_to_long_dictionary_create();
    vertex_reset(); // all vertices unexplored

    vector* non_cycles = vector_create(NULL, NULL, NULL);
    // go through each vertex in G; get rid of rules in cycles
    vector* vertices = graph_neighbors(G, "");
    VECTOR_FOR_EACH(vertices, v, {
        // if vertex is in a cycle, print failure
        if (cycle_detection(v) == 1)
            print_cycle_failure((char*)v);
        // else add to vector
        else{
            vector_push_back(non_cycles, v);
        }
        vertex_reset();
    });
    vector_destroy(vertices);

    if (vector_size(non_cycles) == 0){
        vector_destroy(non_cycles);
        graph_destroy(G);
        dictionary_destroy(v_dict);
        dictionary_destroy(all_tasks);
        pthread_mutex_destroy(&glob_mtx);
        queue_destroy(Q);
        return 0;
    }

    //reset visited dictionary
    vertex_reset();
    // setup tasks for all valid vertices
    VECTOR_FOR_EACH(non_cycles, v, {
        task_setup(v, NULL);
    });

    // init semaphore to keep track of the exit condition
    //printf("num roots: %d\n", num_roots);
    sem_init(&sem, 0, (-1)*num_roots);

    // thread creation
    for (int i = 0; i < (int)num_threads; i++){
        int* tid = malloc(sizeof(int));
        *tid = i + 1;
        pthread_create(&threads[i], NULL, thread_todo, (void*)tid);
    }
    // sem wait (check exit condition)
    //printf("sem wait\n");
    sem_wait(&sem);
    //printf("sem done waiting\n");
    task* voidptr = NULL;
    for (size_t i = 0; i < num_threads; i++){
        queue_push(Q, voidptr);
    }

    void* result;
    for (size_t i = 0; i < num_threads; i++){
        pthread_join(threads[i], &result);
    }

    vertices = graph_vertices(G);
    // walk through task dict and destroy each task
    VECTOR_FOR_EACH(vertices, v, {
        if (dictionary_contains(all_tasks, v)){
            task* curr = *((task**)dictionary_get(all_tasks, v));
            destroy_task(curr);
        }
    });
    vector_destroy(vertices);

    free(threads);
    vector_destroy(non_cycles);
    graph_destroy(G);
    dictionary_destroy(v_dict);
    dictionary_destroy(all_tasks);
    pthread_mutex_destroy(&glob_mtx);
    queue_destroy(Q);
    sem_destroy(&sem);
    return 0;
}
