#ifndef TPOS_PTHREAD_H
#define TPOS_PTHREAD_H

/* Static TransputerOS pthread subset. NULL attributes select defaults. */
typedef struct tp_pthread *pthread_t;
typedef struct { unsigned reserved; } pthread_attr_t;
typedef struct { unsigned reserved; } pthread_mutexattr_t;
typedef struct { unsigned reserved; } pthread_condattr_t;
typedef struct { volatile unsigned locked; } pthread_mutex_t;
typedef struct { volatile unsigned sequence; } pthread_cond_t;

#define PTHREAD_MUTEX_INITIALIZER { 0 }
#define PTHREAD_COND_INITIALIZER { 0 }

int pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
int pthread_join(pthread_t, void **);
pthread_t pthread_self(void);
int pthread_equal(pthread_t, pthread_t);
int pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
int pthread_mutex_destroy(pthread_mutex_t *);
int pthread_mutex_lock(pthread_mutex_t *);
int pthread_mutex_trylock(pthread_mutex_t *);
int pthread_mutex_unlock(pthread_mutex_t *);
int pthread_cond_init(pthread_cond_t *, const pthread_condattr_t *);
int pthread_cond_destroy(pthread_cond_t *);
int pthread_cond_wait(pthread_cond_t *, pthread_mutex_t *);
int pthread_cond_signal(pthread_cond_t *);
int pthread_cond_broadcast(pthread_cond_t *);

#endif
