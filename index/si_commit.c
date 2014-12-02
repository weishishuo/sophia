
/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

#include <libsr.h>
#include <libsv.h>
#include <libsl.h>
#include <libsd.h>
#include <libsi.h>

uint32_t
si_vgc(sra *a, svv *gc)
{
	uint32_t used = 0;
	svv *v = gc;
	while (v) {
		used += sv_vsize(v);
		svv *n = v->next;
		sl *log = (sl*)v->log;
		if (log) {
			sr_gcsweep(&log->gc, 1);
		}
		sr_free(a, v);
		v = n;
	}
	return used;
}

void si_begin(sitx *t, sr *r, si *index, uint64_t vlsn, svlog *log)
{
	t->index = index;
	t->vlsn  = vlsn;
	t->r     = r;
	t->log   = log;
	si_lock(index);
}

void si_commit(sitx *t) {
	si_unlock(t->index);
}

void si_rollback(sitx *t) {
	si_unlock(t->index);
}

static void
si_set(si *index, sr *r, uint64_t vlsn, uint64_t now, svv *v)
{
	index->update_time = now;
	/* match node */
	sriter i;
	sr_iterinit(&i, &si_iter, r);
	sr_iteropen(&i, index, SR_ROUTE, sv_vkey(v), v->keysize);
	sinode *node = sr_iterof(&i);
	assert(node != NULL);
	/* update node */
	svindex *vindex = si_nodeindex(node);
	svv *vgc = NULL;
	sv_indexset(vindex, r, vlsn, v, &vgc);
	node->update_time = index->update_time;
	node->used += sv_vsize(v);
	if (srunlikely(vgc)) {
		uint32_t gc = si_vgc(r->a, vgc);
		node->used -= gc;
		sr_quota(index->quota, SR_QREMOVE, gc);
	}
	/* schedule node */
	si_plannerupdate(&index->p, SI_BRANCH, node);
}

void si_write(sitx *t, int check)
{
	uint64_t now = sr_utime();
	sriter i;
	sr_iterinit(&i, &sr_bufiter, t->r);
	sr_iteropen(&i, &t->log->buf, sizeof(sv));
	for (; sr_iterhas(&i); sr_iternext(&i))
	{
		sv *vp = sr_iterof(&i);
		svv *v = vp->v;
		if (check && si_querycommited(t->index, t->r, vp)) {
			si_vgc(t->r->a, v);
			continue;
		}
		si_set(t->index, t->r, t->vlsn, now, v);
	}
}
