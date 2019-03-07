# Parallel-Make
Attempt at imitating make utility to execute programs for a given Makefile. To simplify
things, I remove all cycles found between dependencies in the Makefile, which I did by
turning the Makefile into a Resource Allocation Graph. I parallelized the execution of 
the dependencies by adding them as 'tasks' to a task queue.
