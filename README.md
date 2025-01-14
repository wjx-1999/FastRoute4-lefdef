FastRoute4-lefdef
======================

**FastRoute4-lefdef** is an open-source global router.

The algorithm base is from FastRoute4.1, and the underlying infrastructure comes from [Rsyn](https://github.com/RsynTeam/rsyn-x/)



The FastRoute4.1 version was received from <yuexu@iastate.edu> on June 15, 2019, with the BSD-3 open source license as given in the FastRoute [website](http://home.eng.iastate.edu/~cnchu/FastRoute.html#License).

[Rsyn](https://github.com/RsynTeam/rsyn-x/) version is based on commit `1087c918e4fa14db84fc3b4c91210db96b07bb4c` and released under Apache License, Version 2.0 as given in its [repository](https://github.com/RsynTeam/rsyn-x/blob/master/README.md).

Any third party code such as Rsyn will clearly specify its license in each file and in this README. We ask contributors to seriously consider using the BSD-3 Licence.

## Getting Started
### Pre-Requisite

- GCC compiler
- boost library
- cmake3.1

### How to Compile

````
git clone --recursive https://github.com/The-OpenROAD-Project/FastRoute4-lefdef
cd FastRoute4-lefdef/
make PARALLEL=nthreads
````
NOTE: use `PARALLEL=nthreads` instead of `-j nthreads`

### Basic Usage

Your command line to run FastRoute4-lefdef should look like this:

````
./FRlefdef --no-gui --script /path/to/script.rsyn
````

You can find a script example in `third_party/rsyn/support/fastroute/example.rsyn`

#### Script details

The basic format of a script to run FastRoute is shown below:

````
open "generic" {
	"lefFiles" : "example.lef",
	"defFiles" : "example.def"
};
run "rsyn.fastRoute" {"outfile" : "example.guide", "adjustment" : 0.X, "maxRoutingLayer" : Y};
````

FastRoute has three main parameters. These are:
- outfile: name of the file with the generated guides
- adjustment: percentage reduction in capacity of each edge in the global routing gridgraph
- maxRoutingLayer: maximum (i.e., highest) routing layer available for FastRoute to use

If you need more than one LEF/DEF file, you can script for this case as:

````
open "generic" {
	"lefFiles" : ["example1.lef", "example2.lef", ..., "exampleN.lef"],
	"defFiles" : ["example1.def", "example2.def", ..., "exampleN.def"]
};
run "rsyn.fastRoute" {"outfile" : "example.guide", "adjustment" : 0.X, "maxRoutingLayer" : Y};
````

You can use either the absolute path or the relative path (relative to the script file) for LEF/DEF files.

## Fixes and modifications on FastRoute4 original code

- Project programming language from C to C++

- Added namespace “FastRoute” in all files
- Code organization:
    - License and folder hierarchy following Tim Ansell’s + Tom Spyru’s guidelines/suggestions
    - Added licence header (as per Iowa website) in each file owned by them
    - Added licence header (BSD-3) for new/created files

- Code clean-up:
    - Remove unused variables
        - Declared not assigned
        - Assigned not read
    - Indentation/code-style for readability

- Setting maximum number of routing layers to 18 \
 &nbsp; &nbsp;&nbsp; &nbsp;   **Previous setting**: Maximum of 8 routing layers \
 &nbsp; &nbsp;&nbsp; &nbsp;   **Reason**: Support for OpenRoad benchmarks

- Added auxiliary function to set layers preferred directions: \
  &nbsp; &nbsp;&nbsp; &nbsp;  **Previous setting**: Hardcoded layers preferred directions \
  &nbsp; &nbsp;&nbsp; &nbsp;  **Reason**: Set layers preferred directions according to each benchmark specification

- Added auxiliary function to get edges capacities \
   &nbsp; &nbsp;&nbsp; &nbsp; **Previous setting**: Cannot get the capacity of a specific edge \
   &nbsp; &nbsp;&nbsp; &nbsp; **Reason**: Access specific edge capacity to verify consistency in capacity adjustments

- Added auxiliary function to get all nets from FastRoute4 data structures \
   &nbsp; &nbsp;&nbsp; &nbsp; **Previous setting**: Cannot access nets from FastRoute4 data structures \
   &nbsp; &nbsp;&nbsp; &nbsp;  **Reason**: Access nets added to FastRoute4 data structures in order to verify if all nets of the design were routed

- Increase maximum net name length \
  &nbsp; &nbsp;&nbsp; &nbsp;  **Previous setting**: Nets added to FastRoute4 data structures can have at most 18 characters \
  &nbsp; &nbsp;&nbsp; &nbsp;  **Reason**: Increasing net name limit to 200 characters, avoiding issues with net names of OpenRoad benchmarks

- Added flag in addAdjustments function allowing capacity increase \
  &nbsp; &nbsp;&nbsp; &nbsp;  **Previous setting**: Capacities adjustments only decrease the original edges capacities \
  &nbsp; &nbsp;&nbsp; &nbsp;  **Reason**: Allow edges capacities increase

- Removing net degree limitation for FastRoute4 \
  &nbsp; &nbsp;&nbsp; &nbsp;  **Previous setting**: Nets can have at most 1000 pins, or they will be ignored \
  &nbsp; &nbsp;&nbsp; &nbsp;  **Reason**: Dynamically memory allocation for data structures dependents on the nets degree, allowing nets to have any degree

- Removing net degree limitation for Flute \
  &nbsp; &nbsp;&nbsp; &nbsp;  **Previous setting**: Nets can have at most 1000 pins, or generates a segfault \
  &nbsp; &nbsp;&nbsp; &nbsp;  **Reason**: Dynamically memory allocation for data structures dependents on the nets degree, allowing nets to have any degree

## FastRoute API

Implemented API to access FastRoute4 as a static library
- Declarations of the functions are in the following path: 	\
        `FastRoute4-lefdef/third_party/fastroute/include/FastRoute.h`
- Implementation of the functions are in the following path: \
        `FastRoute4-lefdef/third_party/fastroute/src/FastRoute.cpp`

The following functions were implemented:

`````
void setGridsandLayers(int x, int y, int nLayers);
`````
This function initializes the grid and layer structures. Three parameters should be passed to this function:

- **x** : number of gcells in x;
- **y** : number of gcells in y;
- **nLayers** : number of layers.

````
void addVCapacity(int verticalCapacity, int layer);
`````
This function adds a layer vertical capacity. Two parameters should be passed to this function:
- **verticalCapacity** : capacity of vertical edges in a layer;
- **layer** : layer number. 

`````
void addHCapacity(int horizontalCapacity, int layer);
`````
This function adds a layer horizontal capacity. Two parameters should be passed to this function:
- **horizontalCapacity** : capacity of horizontal edges in a layer;
- **layer** : layer number.

````
void addMinWidth(int width, int layer);
`````
This function adds wire minimum width for a specific layer. Two parameters should be passed to this function:
- **width** : minimum wire width value;
- **layer** : layer number.

````
void addMinSpacing(int spacing, int layer);
`````
This function adds a layer minimum spacing. Two parameters should be passed to this function:
- **spacing** : minimum spacing value;
- **layer** : layer number.

````
void addViaSpacing(int spacing, int layer);
````
This function adds via minimum spacing for a specific layer. Two parameters should be passed to this function:
- **spacing** : via minimum spacing value;
- **layer** : layer number .

````
void setNumberNets(int nNets);
`````
This function sets the total number of nets. One parameter should be passed to this function:
- **nNets** : total number of nets.

````
void setLowerLeft(int x, int y);
````
This function sets the lower left bounds. Two parameters should be passed to this function:
- **x** : lower left x position;
- **y** : lower left y position.

````
void setTileSize(int width, int height);
````
This function sets width and height of a gcell. Two parameters should be passed to this function:
- **width** : gcell width value;
- **height** : gcell height value.

````
void addNet(char *name, int netIdx, int nPins, int minWidth, PIN pins[]);
````
This function adds a net structure. Five parameters should be passed to this function:
- **name** : net name;
- **netIdx** : net index;
- **nPins** : number of pins of the net;
- **minWidth** : net minimum width;
- **pins** : vector containing the pins of the net.

````
void initEdges();
````
This function initializes grid edges; this function must be called right after all nets were added.

````
void setNumAdjustments(int nAdjustments);
````
This function sets the number of adjustments to be made. One parameter should be passed to this function:
- **nAdjustments** : number of adjustments.

````
void addAdjustment(long x1, long y1, int l1, long x2, long y2, int l2, int reducedCap, bool isReduce = true);
`````
This function adds a capacity adjustment to a specific edge. Eight parameters should be passed to this function:
- **x1** : x position of one of the gcells to which the edge belongs to;
- **y1** : y position of the above gcell;
- **l1** : layer number of the above gcell;
- **x2** : x position of the second gcell to which the edge belongs to;
- **y2** : y position of the above gcell;
- **l2** : layer number of the above gcell;
- **reducedCap** : new edge capacity;
- **isReduce** : set to true if the capacity is going to be reduced, to false if it is going to be increased.

````
void initAuxVar();
````
This function initializes auxiliary variables; this function must be called right before the run function.

````
void run(std::vector<NET> &);
````
This function runs the fastroute algorithm, a vector of NETs should be passed as parameter; this is where the final result will be stored.

Copyright (c) 2019, Federal University of Rio Grande do Sul (UFRGS)
All rights reserved.