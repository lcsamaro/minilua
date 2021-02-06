#ifndef VECTOR_H
#define VECTOR_H

#define vdef(name, type, cap)   \
	typedef struct {        \
		int size;       \
		type data[cap]; \
	} name;

#define vclear(v)   do { v.size = 0; } while (0)
#define vsize(v)    (v.size)
#define vcap(v)     (sizeof(v.data)/sizeof(v.data[0]))

#define vempty(v)   (v.size == 0)
#define vfull(v)    (v.size == vcap(v))

#define vpush(v,e)  (v.data[v.size++] = e)
#define vpop(v)     (v.data[--v.size])

#define vget(v,i)   (v.data[i])
#define vset(v,i,e) (v.data[i] = e)

#define vbegin(v)   (v.data)
#define vfront(v)   (v.data[0])

#define vend(v)     (v.data+v.size)
#define vback(v)    (v.data[v.size-1])

#endif /* VECTOR_H */

