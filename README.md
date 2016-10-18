---------------------
What is this?
---------------------
A very simplistic command line tool to create dependency graphs of c++ projects.

---------------------
How do I build it?
---------------------
Provided you have Qt, `qmake -project && qmake && make`

---------------------
How do I use it?
---------------------
Provided you have GraphViz installed for `dot` and `tred`:

```./include_dependency_graph settings.ini <paths> | tred | dot -Tpng -odependency_graph.png  ```

---------------------
How may I use it?
---------------------
You can use it as you wish. No limitations and no guarantees.
