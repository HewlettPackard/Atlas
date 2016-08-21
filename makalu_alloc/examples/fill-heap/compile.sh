
gcc -O0 -pthread  \
-I ../../region_manager \
-I ../../include \
-o fill_start \
fill-start.c \
../../region_manager/region_manager.c \
../../lib/libmakalu.a ../../libatomic/lib/libatomic_ops.a

gcc -O0 -pthread  \
-I ../../region_manager \
-I ../../include \
-o fill_recover \
fill-recover.c \
../../region_manager/region_manager.c \
../../lib/libmakalu.a ../../libatomic/lib/libatomic_ops.a
