/**
* Parallel Make Lab
* CS 241 - Spring 2018
*/

#include "format.h"
#include "graph.h"
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

graph* G;
dictionary* v_dict;

/********  HELPERS  ********/

//set all vertices to unexplored
void vertex_reset();

//cycle detection on a single veretx; if there is a cross edge, return 1; otherwise, 0
int cycle_detection(void* v);

//execute commands of a rule
int exec_cmds(void* v);

//check if a rule is a file on disk; return 1 for no, 0 for yes
int check_no_file_disk(void* v, struct stat orig_buf);

/********   DONE   ********/

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
    if (*((long*)dictionary_get(v_dict, v)) == 0){ // if node has not been visited
        dictionary_set(v_dict, v, (void*)&one); // set visited
        vector* neighbors = graph_neighbors(G, v);
        VECTOR_FOR_EACH(neighbors, adj, {
            //printf("%s val: %ld\n", (char*)adj, *((long*)dictionary_get(v_dict, adj)));
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
    if (out == -1)
        return 1;
    //only other way exec_flag can be 1 is if this is a newer file
    if (buf.st_mtime > orig_buf.st_mtime)
        return 1;
    return 0;
}

int exec_cmds(void* v){
    int exec_flag = 0;
    rule_t *rule = (rule_t *)graph_get_vertex_value(G, v);
    if (rule->state != 0)
        return 1; // end if rule is already done

    // get stat for current rule
    struct stat orig_buf;
    int out = stat(rule->target, &orig_buf);
    if (out == -1)
        exec_flag = 1;

    vector* neighbors = graph_neighbors(G, v);
    VECTOR_FOR_EACH(neighbors, adj, {
        rule_t *adj_rule = (rule_t *)graph_get_vertex_value(G, adj);
        if (adj_rule->state == 0){ // not finished yet
            out = exec_cmds(adj);
            if (out == 0){
                rule->state = 2; // finished, failed
                vector_destroy(neighbors);
                return 0;
            }
        }
        else if (adj_rule->state == 2){ // finished but failed, rule fails
            rule->state = 2; // failed
            vector_destroy(neighbors);
            return 0;
        }
    });
    vector_destroy(neighbors);

    if (!exec_flag){
        vector* neighbors = graph_neighbors(G, v);
        VECTOR_FOR_EACH(neighbors, adj, {
            if (check_no_file_disk(adj, orig_buf) == 1){
                exec_flag = 1;
                break;
            }
        });
        vector_destroy(neighbors);
    }

    if (exec_flag){
        VECTOR_FOR_EACH(rule->commands, cmd, {
            int out = system(cmd);
            if (out == -1){
                rule->state = 2; // finished; failed
                return 0;
            }
        });
    }
    rule->state = 1; // finished
    return 1;
}

int parmake(char *makefile, size_t num_threads, char **targets){
    // get graph with all rules and dependencies
    G = parser_parse_makefile(makefile, targets);
    v_dict = string_to_long_dictionary_create();
    vertex_reset(); // all vertices unexplored

    // go through each vertex in G
    vector* vertices = graph_neighbors(G, "");
    VECTOR_FOR_EACH(vertices, v, {
        // if vertex is in a cycle, print failure
        if (cycle_detection(v) == 1){
            print_cycle_failure((char*)v);
            rule_t *rule = (rule_t *)graph_get_vertex_value(G, v);
            rule->state = 2; // finished; failed
        }
        // have covered possible cycles for rule and its dependencies
        else{ // execute cmds
            exec_cmds(v);
        }
        vertex_reset();
    });
    vector_destroy(vertices);

    graph_destroy(G);
    dictionary_destroy(v_dict);
    return 0;
}
