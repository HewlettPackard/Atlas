# Grand Contraption
[Slides](https://hpenterprise-my.sharepoint.com/personal/kartik_singhal_hpe_com/_layouts/15/guestaccess.aspx?guestaccesstoken=WJCdIWG3pO1uHguoplr6V%2b8XMQr1NeQaZj8GvA22MHM%3d&docid=2_02d053a3b98de4e5a87e54b84aa9870af)

# Data Structure Design

## Graph Design

A growable array indexed by vertex numbers serves as the entry-point for the graph store. Each entry is a `struct` as follows:
```C
struct Node {
  mutex m;
  NodeInfo *primary;
  DeltaEntry *delta;
};
```

where `NodeInfo` is:
```C
struct NodeInfo {
  Edge in[]; // growable array
  Edge out[]; // ditto
  Data info; // arbitrary data associated with this node
};
```

and `DeltaEntry`:
```C
struct DeltaEntry {
  LOG_TYPE op;
  void *info;
  DeltaEntry *next;
};
```

`LOG_TYPE` is an `enum` defining the operations supported on a node:
```C
enum LOG_TYPE { GET, UPSERT };
```

`Edge` stores edge data and the identifier (index) of the other vertex:
```C
struct Edge {
  unsigned int otherEnd;
  Data *info;
};
```

When a vertex is freed, its identifying integer is recycled by pushing it into a stack of recyclable integers. This prevents the array storing `Node`s from growing unnecessarily.

## Global Data

| Name          | Initial Value | Transition         |
| ------------- | -------------:| ------------------:|
| Snapshot #    |             0 |                 +1 |
| Snapshot Flag |             F |                T/F |
|                                                    |
| Thread #1     |        (0, F) |  (Snapshot #, T/F) |
| Thread #2     |        (0, F) |  (Snapshot #, T/F) |
| Thread #...   |           ... |                ... |
| Thread #n     |        (0, F) |  (Snapshot #, T/F) |

There is a global snapshot number, `Snapshot #`, which is incremented only by the snaphot thread and is read by all the worker threads. `Snapshot Flag` is `True` when a snapshot is requested or is in progress and is turned ON/OFF only by the snapshot thread. Each worker thread needs to query these two global fields to decide whether to update a vertex `primary` container directly or to write to the `delta` container; they cache this information in their corresponding `(Snapshot #, Snapshot Flag)` pairs to avoid contention on the snapshot state variables. Once all worker threads reach consensus  on `SS#` in their `(SS#, True)` pairs, the snapshot thread can proceed to take a snapshot.

## Supported Low-Level Operations
These presume all required locks are taken by the caller.

### Snapshot
As described above. During the snapshot process, none of the worker threads write to the `primary` container, but only to the `delta` list. `primary` container is treated as read only and a number of threads can be employed to take the snapshot.

### Upsert

### Get

## Supported High-Level Operations
High level operations can be treated as transactions (group of ordered low-level operations). They are responsible for taking all the required locks needed for a txn, checking if a snapshot is in progress and update the snapshot state pair of the worker thread accordingly, and, finally, to call low-level functions to do the actual job.
### Add vertex
### Remove vertex
### Add edge
### Remove edge
### Read vertex/edge info
### Update vertex/edge info
