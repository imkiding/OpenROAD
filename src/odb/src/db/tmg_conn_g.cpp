///////////////////////////////////////////////////////////////////////////////
// BSD 3-Clause License
//
// Copyright (c) 2019, Nefelus Inc
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// * Neither the name of the copyright holder nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include <cstdio>
#include <cstdlib>

#include "db.h"
#include "dbShape.h"
#include "dbWireCodec.h"
#include "tmg_conn.h"
#include "utl/Logger.h"

namespace odb {

using utl::ODB;

struct tcg_edge;
struct tcg_edge
{
  tcg_edge* next;
  tcg_edge* reverse;
  tmg_rcshort* s;
  int fr;
  int to;
  int k;  // index to _rcV
  bool visited;
  bool skip;
};
struct tcg_pt
{
  tcg_edge* edges;
  int ipath;
  int visited;  // 1= from another descent, 2+k= _stackV[k]->fr
};

class tmg_conn_graph
{
 public:
  tcg_pt* _ptV;
  int _ptN;
  int* _path_vis;
  tcg_edge** _stackV;
  int _stackN;
  tcg_edge* _e;

 private:
  int _ptNmax;
  int _shortNmax;
  int _eNmax;
  tcg_edge* _eV;
  int _eN;

 public:
  tmg_conn_graph();
  void init(int ptN, int shortN);
  tcg_edge* newEdge(tmg_conn* conn, int fr, int to)
  {
    tcg_edge* e = _eV + _eN++;
    e->k = -1;
    e->skip = false;
    int ndx = conn->_ptV[to]._x;
    int ndy = conn->_ptV[to]._y;
    tcg_edge* ppe = nullptr;
    tcg_edge* pe = _ptV[fr].edges;
    while (pe && !pe->s && ndx > conn->_ptV[pe->to]._x) {
      ppe = pe;
      pe = pe->next;
    }
    while (pe && !pe->s && ndx == conn->_ptV[pe->to]._x
           && ndy > conn->_ptV[pe->to]._y) {
      ppe = pe;
      pe = pe->next;
    }
    e->next = pe;
    if (ppe)
      ppe->next = e;
    else
      _ptV[fr].edges = e;
    return e;
  }
  tcg_edge* newShortEdge(tmg_conn* conn, int fr, int to)
  {
    tcg_edge* e = _eV + _eN++;
    e->k = -1;
    e->skip = false;
    int ned = conn->ptDist(fr, to);
    int ndx = conn->_ptV[to]._x;
    int ndy = conn->_ptV[to]._y;
    tcg_edge* ppe = nullptr;
    tcg_edge* pe = _ptV[fr].edges;
    while (pe && ned > conn->ptDist(pe->fr, pe->to)) {
      ppe = pe;
      pe = pe->next;
    }
    while (pe && ned == conn->ptDist(pe->fr, pe->to)
           && ndx > conn->_ptV[pe->to]._x) {
      ppe = pe;
      pe = pe->next;
    }
    while (pe && ned == conn->ptDist(pe->fr, pe->to)
           && ndx == conn->_ptV[pe->to]._x && ndy > conn->_ptV[pe->to]._y) {
      ppe = pe;
      pe = pe->next;
    }
    e->next = pe;
    if (ppe)
      ppe->next = e;
    else
      _ptV[fr].edges = e;
    return e;
  }
  tcg_edge* getNextEdge(bool ok_to_descend);
  tcg_edge* getFirstEdge(int jstart);
  tcg_edge* getFirstNonShortEdge(int& jstart);
  void addEdges(tmg_conn* conn, int i0, int i1, int k);
  void clearVisited();
  void relocateShorts(tmg_conn* conn);
  void getEdgeRefCoord(tmg_conn* conn, tcg_edge* pe, int& rx, int& ry);
  uint isBadShort(tcg_edge* pe, tmg_conn* conn);
};

tmg_conn_graph::tmg_conn_graph()
{
  _ptNmax = 1024;
  _shortNmax = 1024;
  _eNmax = 1024;
  _ptV = (tcg_pt*) malloc(_ptNmax * sizeof(tcg_pt));
  _path_vis = (int*) malloc(_ptNmax * sizeof(int));
  _eV = (tcg_edge*) malloc(2 * _ptNmax * sizeof(tcg_edge));
  _stackV = (tcg_edge**) malloc(_shortNmax * sizeof(tcg_edge*));
}

void tmg_conn_graph::init(int ptN, int shortN)
{
  if (ptN > _ptNmax) {
    _ptNmax = 2 * ptN;
    free(_ptV);
    _ptV = (tcg_pt*) malloc(_ptNmax * sizeof(tcg_pt));
    free(_path_vis);
    _path_vis = (int*) malloc(_ptNmax * sizeof(int));
  }
  if (shortN > _shortNmax) {
    _shortNmax = 2 * shortN;
    free(_stackV);
    _stackV = (tcg_edge**) malloc(_shortNmax * sizeof(tcg_edge*));
  }
  if (4 * ptN + 2 * shortN > _eNmax) {
    _eNmax *= 2;
    if (4 * ptN + 2 * shortN > _eNmax)
      _eNmax = 4 * ptN + 2 * shortN;
    free(_eV);
    _eV = (tcg_edge*) malloc(_eNmax * sizeof(tcg_edge));
  }
  _eN = 0;
  int j;
  for (j = 0; j < ptN; j++)
    _ptV[j].edges = nullptr;
  _ptN = ptN;
}

void tmg_conn_graph::clearVisited()
{
  int j;
  for (j = 0; j < _eN; j++)
    _eV[j].visited = false;
  for (j = 0; j < _ptN; j++)
    _ptV[j].visited = 0;
}

void tmg_conn_graph::getEdgeRefCoord(tmg_conn* conn,
                                     tcg_edge* pe,
                                     int& rx,
                                     int& ry)
{
  rx = conn->_ptV[pe->to]._x;
  ry = conn->_ptV[pe->to]._y;
  if (pe->s == nullptr)
    return;
  tcg_edge* se = _ptV[pe->to].edges;
  while (se && se->s)
    se = se->next;
  if (se == nullptr)
    return;
  rx = conn->_ptV[se->to]._x;
  ry = conn->_ptV[se->to]._y;
}

uint tmg_conn_graph::isBadShort(tcg_edge* pe, tmg_conn* conn)
{
  if (pe->s == nullptr)
    return 0;
  if (conn->_ptV[pe->fr]._x != conn->_ptV[pe->to]._x)
    return 1;
  if (conn->_ptV[pe->fr]._y != conn->_ptV[pe->to]._y)
    return 1;
  return 0;
}

void tmg_conn_graph::relocateShorts(tmg_conn* conn)
{
  tcg_edge *pe, *ppe, *pppe, *skipe, *last;
  int jp, r1x, r1y, r2x, r2y;
  bool needAdjust, firstCheck;
  for (jp = 0; jp < _ptN; jp++) {
    pe = _ptV[jp].edges;
    if (pe == nullptr || pe->next == nullptr)
      continue;
    needAdjust = true;
    while (needAdjust) {
      needAdjust = false;
      firstCheck = true;
      pppe = nullptr;
      ppe = nullptr;
      for (pe = _ptV[jp].edges; pe != nullptr; pe = pe->next) {
        if (ppe == nullptr) {
          ppe = pe;
          continue;
        }
        if (firstCheck)
          getEdgeRefCoord(conn, ppe, r1x, r1y);
        firstCheck = false;
        getEdgeRefCoord(conn, pe, r2x, r2y);
        if ((pe->s == nullptr && ppe->s == nullptr) || isBadShort(pe, conn)
            || isBadShort(ppe, conn)) {
          pppe = ppe;
          ppe = pe;
          r1x = r2x;
          r1y = r2y;
          continue;
        }
        if (r1x > r2x || (r1x == r2x && r1y > r2y)) {
          needAdjust = true;
          last = pe->next;
          if (pppe)
            pppe->next = pe;
          else
            _ptV[jp].edges = pe;
          pppe = pe;
          pe->next = ppe;
          ppe->next = last;
          pe = ppe;
        } else {
          pppe = ppe;
          ppe = pe;
          r1x = r2x;
          r1y = r2y;
        }
      }
    }
  }
  // re-assign "skip".
  int noshortn, shortn, skipn;
  bool newSkip;
  tcg_edge *plast, *nse;
  for (jp = 0; jp < _ptN; jp++) {
    skipe = nullptr;
    noshortn = 0;
    shortn = 0;
    skipn = 0;
    ppe = nullptr;
    plast = nullptr;
    last = nullptr;
    for (pe = _ptV[jp].edges; pe != nullptr; pe = pe->next) {
      if (!pe->s) {
        noshortn++;
        ppe = pe;
        continue;
      }
      shortn++;
      if (isBadShort(pe, conn)) {
        continue;  // bad short
      }
      if (pe->skip) {
        if (!skipe)
          skipe = pe;
        skipn++;
      }
      plast = last;
      last = pe;
    }
    if (!skipe)
      continue;  // no need to adjust skip
    if (noshortn <= 1)
      continue;  // adjust only the long (main) branch
    if (shortn <= 1)
      continue;  //  bp. because skipe != nullptr
    if (!plast)
      continue;  // may happen with bad short    wfs 6-27-06
    if (skipn > 1)
      pppe = pe;  // bp.
    if (shortn > 2)
      pppe = pe;  // bp.
    newSkip = false;
    // plast->to and last->to is the short pair to skip
    // do skip new pair;
    for (nse = _ptV[plast->to].edges; nse != nullptr && nse->to != last->to;
         nse = nse->next) {
      ;
    }
    if (nse && nse->s) {
      nse->s->_skip = true;
      nse->skip = true;
      nse->reverse->skip = true;
      newSkip = true;
    } else
      newSkip = false;

    if (!newSkip)
      return;
    // unskip skipe
    skipe->s->_skip = false;
    skipe->skip = false;
    skipe->reverse->skip = false;
  }
  return;
}

tcg_edge* tmg_conn_graph::getFirstNonShortEdge(int& jstart)
{
  if (_ptV[jstart].visited || !_ptV[jstart].edges)
    return nullptr;
  tcg_edge* e = _ptV[jstart].edges;
  while (e && (e->visited || e->skip))
    e = e->next;
  if (!e)
    return nullptr;
  uint loops = 16;
  while (loops && e->s) {
    jstart = jstart == e->s->_i0 ? e->s->_i1 : e->s->_i0;
    e = _ptV[jstart].edges;
    loops--;
  }
  if (loops == 0) {
    e = nullptr;
  }
  if (!e) {
    _stackV[0] = nullptr;
    _stackN = 0;
    return nullptr;
  }
  _stackV[0] = e;
  _stackN = 1;
  return e;
}

tcg_edge* tmg_conn_graph::getFirstEdge(int jstart)
{
  if (_ptV[jstart].visited || !_ptV[jstart].edges)
    return nullptr;
  tcg_edge* e = _ptV[jstart].edges;
  while (e && (e->visited || e->skip))
    e = e->next;
  if (!e)
    return nullptr;
  _stackV[0] = e;
  _stackN = 1;
  return e;
}

tcg_edge* tmg_conn_graph::getNextEdge(bool ok_to_descend)
{
  tcg_edge *e, *e2;
  e = _stackV[_stackN - 1];

  if (ok_to_descend) {
    e2 = _ptV[e->to].edges;
    while (e2 && (e2->visited || e2->skip))
      e2 = e2->next;
    if (e2) {
      if (_stackN >= _shortNmax) {
        _shortNmax = _shortNmax * 2;
        _stackV = (tcg_edge**) realloc(_stackV, _shortNmax * sizeof(tcg_edge*));
      }
      _stackV[_stackN++] = e2;
      return e2;
    }
  }
  // sibling
  // avoid resetting loop node
  if (_ptV[e->to].visited > _ptV[e->fr].visited)
    _ptV[e->to].visited = 1;
  e = e->next;
  while (e && (e->visited || e->skip))
    e = e->next;
  if (e) {
    _stackV[_stackN - 1] = e;
    return e;
  }
  // ascend
  while (--_stackN > 0) {
    e = _stackV[_stackN - 1];
    _ptV[e->to].visited = 1;
    e = e->next;
    while (e && (e->visited || e->skip))
      e = e->next;
    if (e) {
      _stackV[_stackN - 1] = e;
      return e;
    }
  }
  return nullptr;
}

void tmg_conn::relocateShorts()
{
  _graph->relocateShorts(this);
}

void tmg_conn::removeShortLoops()
{
  if (!_graph)
    _graph = new tmg_conn_graph();
  _graph->init(_ptV.size(), _shortN);
  tcg_pt* pgV = _graph->_ptV;
  tmg_rcshort* s;
  tcg_edge *e, *e2;

  // setup paths
  int npath = -1;
  for (unsigned long j = 0; j < _rcV.size(); j++) {
    if (j == 0 || _rcV[j]._ifr != _rcV[j - 1]._ito) {
      ++npath;
    }
    pgV[_rcV[j]._ifr].ipath = npath;
    pgV[_rcV[j]._ito].ipath = npath;
  }
  npath++;

  // remove shorts to same path
  for (int j = 0; j < _shortN; j++) {
    s = _shortV + j;
    if (s->_skip)
      continue;
    if (pgV[s->_i0].ipath == pgV[s->_i1].ipath) {
      s->_skip = true;
    }
  }

  for (int j = 0; j < _shortN; j++) {
    s = _shortV + j;
    if (s->_skip)
      continue;
    for (e = pgV[s->_i0].edges; e; e = e->next)
      if (e->to == s->_i1)
        break;
    if (e) {
      s->_skip = true;
      continue;
    }
    e = _graph->newShortEdge(this, s->_i0, s->_i1);
    e2 = _graph->newShortEdge(this, s->_i1, s->_i0);
    e->s = s;
    e2->s = s;
    e->reverse = e2;
    e2->reverse = e;
    e->fr = s->_i0;
    e->to = s->_i1;
    e2->fr = s->_i1;
    e2->to = s->_i0;
    e->visited = false;
    e2->visited = false;
  }

  tcg_pt* pg;
  int* path_vis = _graph->_path_vis;
  int jstart;
  for (int j = 0; j < _ptV.size(); j++) {
    pgV[j].visited = 0;
  }

  // remove all short loops
  _graph->clearVisited();
  for (int j = 0; j < npath; j++)
    path_vis[j] = 0;
  for (jstart = 0; jstart < _ptV.size(); jstart++) {
    e = _graph->getFirstEdge(jstart);
    if (!e)
      continue;
    pgV[jstart].visited = 2;
    while (e) {
      e->visited = 1;
      e->reverse->visited = 1;
      pg = pgV + e->to;
      if (pg->visited) {
        e->skip = true;
        e->reverse->skip = true;
        e->s->_skip = true;
        e = _graph->getNextEdge(false);
      } else {
        pg->visited = 2 + _graph->_stackN;
        e = _graph->getNextEdge(true);
      }
    }
  }

  // count components, and remaining loops
  _graph->clearVisited();
  for (jstart = 0; jstart < _ptV.size(); jstart++) {
    e = _graph->getFirstEdge(jstart);
    if (!e)
      continue;
    pgV[jstart].visited = 2;
    while (e) {
      e->visited = 1;
      e->reverse->visited = 1;
      pg = pgV + e->to;
      if (pg->visited) {
        e = _graph->getNextEdge(false);
      } else {
        pg->visited = 2 + _graph->_stackN;
        e = _graph->getNextEdge(true);
      }
    }
  }
}

void tmg_conn_graph::addEdges(tmg_conn* conn, int i0, int i1, int k)
{
  tcg_edge *e, *e2;
  e = newEdge(conn, i0, i1);
  e2 = newEdge(conn, i1, i0);
  e->s = nullptr;
  e->reverse = e2;
  e->fr = i0;
  e->to = i1;
  e->k = k;
  e->visited = false;
  e->skip = false;
  e2->s = nullptr;
  e2->reverse = e;
  e2->fr = i1;
  e2->to = i0;
  e2->k = k;
  e2->visited = false;
  e2->skip = false;
}

void tmg_conn::removeWireLoops()
{
  removeShortLoops();

  // loops involving only shorts have already been handled
  if (_rcV.empty())
    return;
  // add all path edges
  for (unsigned long j = 0; j < _rcV.size(); j++) {
    _graph->addEdges(this, _rcV[j]._ifr, _rcV[j]._ito, j);
  }

  // remove loops that have shorts by removing
  // the short with max distance
  // if no shorts, allow the loop to stay;
  // we do not expect any router to have a pure path loop

  int done = 0;
  int jstart, k;
  tcg_pt *pg, *pgV = _graph->_ptV;
  tcg_edge *e, *eloop, *emax;
  int dist, max_dist, max_k;
  while (!done) {
    int loop_removed = 0;
    done = 1;
    _graph->clearVisited();
    for (jstart = 0; jstart < _ptV.size(); jstart++) {
      e = _graph->getFirstEdge(jstart);
      if (!e)
        continue;
      pgV[jstart].visited = 2;
      while (e) {
        e->visited = 1;
        e->reverse->visited = 1;
        pg = pgV + e->to;
        if (pg->visited == 1) {
          done = 0;
        } else if (pg->visited) {
          k = pg->visited - 2;
          max_dist = 0;
          max_k = 0;
          emax = nullptr;
          for (; k < _graph->_stackN; k++) {
            eloop = _graph->_stackV[k];
            if (!eloop->s)
              continue;
            dist = abs(_ptV[eloop->fr]._x - _ptV[eloop->to]._x)
                   + abs(_ptV[eloop->fr]._y - _ptV[eloop->to]._y);
            if (dist >= max_dist) {
              max_dist = dist;
              max_k = k;
              emax = eloop;
            }
          }
          if (!emax) {
            done = 0;
          } else {
            emax->skip = true;
            emax->reverse->skip = true;
            emax->s->_skip = true;
            loop_removed++;
            done = 0;
            if (max_k + 1 < _graph->_stackN) {
              int k2;
              for (k2 = max_k + 1; k2 < _graph->_stackN - 1; k2++)
                pgV[_graph->_stackV[k2]->to].visited = 1;
              _graph->_stackN = max_k + 1;
            }
          }
        }
        if (pg->visited) {
          e = _graph->getNextEdge(false);
        } else {
          pg->visited = 2 + _graph->_stackN;
          e = _graph->getNextEdge(true);
        }
      }
    }
    if (!loop_removed)
      break;
  }

  // report all remaining loops, and count components
  _graph->clearVisited();
  for (jstart = 0; jstart < _ptV.size(); jstart++) {
    e = _graph->getFirstEdge(jstart);
    if (!e)
      continue;
    pgV[jstart].visited = 2;
    while (e) {
      e->visited = 1;
      e->reverse->visited = 1;
      pg = pgV + e->to;
      if (pg->visited) {
        k = pg->visited - 2;
        if (k >= 0)
          for (; k < _graph->_stackN; k++) {
            eloop = _graph->_stackV[k];
          }
        e = _graph->getNextEdge(false);
      } else {
        pg->visited = 2 + _graph->_stackN;
        e = _graph->getNextEdge(true);
      }
    }
  }
}

void tmg_conn::dfsClear()
{
  _graph->clearVisited();
}

bool tmg_conn::dfsStart(int& j)
{
  _graph->_e = _graph->getFirstNonShortEdge(j);
  if (!_graph->_e)
    return false;
  return true;
}

bool tmg_conn::dfsNext(int* from,
                       int* to,
                       int* k,
                       bool* is_short,
                       bool* is_loop)
{
  tcg_edge* e = _graph->_e;
  tcg_pt* pgV = _graph->_ptV;
  if (!e)
    return false;
  *from = e->fr;
  *to = e->to;
  *k = e->k;
  *is_short = (e->s ? true : false);
  e->visited = 1;
  e->reverse->visited = 1;
  pgV[e->fr].visited = 1;
  if (pgV[e->to].visited) {
    *is_loop = true;
    _graph->_e = _graph->getNextEdge(false);
  } else {
    *is_loop = false;
    pgV[e->to].visited = 1;
    _graph->_e = _graph->getNextEdge(true);
  }
  return true;
}

int tmg_conn::isVisited(int j)
{
  return _graph->_ptV[j].visited;
}

void tmg_conn::checkVisited()
{
  int j;
  tcg_pt* pgV = _graph->_ptV;
  for (j = 0; j < _ptV.size(); j++)
    if (!pgV[j].visited) {
      _connected = false;
      break;
    }
}

void tmg_conn::printDisconnect()
{
  int j, n, jsmall = 0;
  int compn = 0;
  int nsmall = 0;
  int tstack0 = 0;
  int tstackN = 0;
  tmg_rcterm *x, **tstackV = _tstackV;
  tcg_pt* pgV = _graph->_ptV;
  tcg_edge* e;
  _graph->clearVisited();
  for (j = 0; j < _ptV.size(); j++) {
    e = _graph->getFirstEdge(j);
    if (!e)
      continue;
    if (_ptV[j]._tindex >= 0)
      tstackV[tstackN++] = _termV + _ptV[j]._tindex;
    compn++;
    pgV[j].visited = 1;
    n = 0;
    while (1) {
      // do a physically-connected subtree
      while (e) {
        n++;
        e->visited = 1;
        e->reverse->visited = 1;
        if (_ptV[e->to]._tindex >= 0) {
          x = _termV + _ptV[e->to]._tindex;
          if (x->_pt && x->_pt->_next_for_term)
            tstackV[tstackN++] = x;
        }
        if (pgV[e->to].visited) {
          e = _graph->getNextEdge(false);
        } else {
          pgV[e->to].visited = 1;
          e = _graph->getNextEdge(true);
        }
      }
      // finished physically-connected subtree,
      // find an unvisited short-from term point
      tmg_rcpt* pt = nullptr;
      while (tstack0 < tstackN && !pt) {
        x = tstackV[tstack0++];
        for (pt = x->_pt; pt; pt = pt->_next_for_term)
          if (!pgV[pt - &_ptV[0]].visited)
            break;
      }
      if (!pt)
        break;
      tstack0--;
      e = _graph->getFirstEdge(pt - &_ptV[0]);
      pgV[pt - &_ptV[0]].visited = 1;
    }
    if (nsmall == 0 || n < nsmall) {
      nsmall = n;
      jsmall = j;
    }
  }
  if (compn < 2 || nsmall == 0)
    return;
  _graph->clearVisited();
  e = _graph->getFirstEdge(jsmall);
  pgV[jsmall].visited = 1;
  while (e) {
    e->visited = 1;
    e->reverse->visited = 1;
    if (pgV[e->to].visited) {
      e = _graph->getNextEdge(false);
    } else {
      pgV[e->to].visited = 1;
      e = _graph->getNextEdge(true);
    }
  }
}

void tmg_conn::adjustShapes()
{
  int j, k;
  tcg_pt *p, *q, *pgV = _graph->_ptV;
  tcg_edge* e;
  int pS[256], p0, pN;
  tmg_rc_sh *s, *sV[256];
  tmg_rc* rV[256];
  // since the dbShape for a via does not carry a ref point,
  // we keep this in an auxiliary array
  tmg_rcpt* spV[256];
  int is_h[256], is_v[256];
  int sN;

  // find shorts that are not to the same xy
  _graph->clearVisited();
  for (j = 0; j < _ptV.size(); j++) {
    p = pgV + j;
    if (p->visited)
      continue;
    p->visited = 1;
    pS[0] = j;
    pN = 1;
    for (e = p->edges; e; e = e->next)
      if (!e->skip && e->s) {
        pS[pN++] = e->to;
        pgV[e->to].visited = 1;
      }
    if (pN == 1)
      continue;
    p0 = 1;
    while (p0 < pN) {
      q = pgV + pS[p0++];
      for (e = q->edges; e; e = e->next)
        if (!e->skip && e->s) {
          for (k = 0; k < pN; k++)
            if (e->to == pS[k])
              break;
          if (k == pN) {
            pS[pN++] = e->to;
            pgV[e->to].visited = 1;
          }
        }
    }
    for (k = 1; k < pN; k++)
      if (_ptV[pS[k]]._x != _ptV[j]._x || _ptV[pS[k]]._y != _ptV[j]._y)
        break;
    if (k == pN)
      continue;

    // we have pN points
    // now get all sN>=pN shapes connected to them
    sN = 0;
    int nvia = 0;
    int first_seg = 1;
    int ok_hor = 1;
    int ok_ver = 1;
    int xlo = 0;
    int xhi = 0;
    int ylo = 0;
    int yhi = 0;
    int w = 0;
    for (k = 0; k < pN; k++)
      for (e = pgV[pS[k]].edges; e; e = e->next)
        if (!e->s) {
          spV[sN] = &_ptV[pS[k]];
          rV[sN] = &_rcV[e->k];
          s = &(_rcV[e->k]._shape);
          sV[sN] = s;
          sN++;
          if (s->isVia())
            nvia++;
          else if (first_seg) {
            if (s->getDX() < s->getDY())
              w = s->getDX();
            else
              w = s->getDY();
            first_seg = 0;
            xlo = s->xMin();
            xhi = s->xMax();
            ylo = s->yMin();
            yhi = s->yMax();
          } else {
            if (s->getDX() < s->getDY()) {
              if ((uint) w != s->getDX())
                w = 0;
            } else {
              if ((uint) w != s->getDY())
                w = 0;
            }
            if (s->xMin() != xlo || s->xMax() != xhi) {
              ok_ver = 0;
              if (s->xMin() < xlo)
                xlo = s->xMin();
              if (s->xMax() > xhi)
                xhi = s->xMax();
            }
            if (s->yMin() != ylo || s->yMax() != yhi) {
              ok_hor = 0;
              if (s->yMin() < ylo)
                ylo = s->yMin();
              if (s->yMax() > yhi)
                yhi = s->yMax();
            }
          }
        }

    for (k = 0; k < sN; k++) {
      if (sV[k]->isVia()) {
        is_h[k] = 0;
        is_v[k] = 0;
      } else {
        is_h[k]
            = (sV[k]->xMax() - sV[k]->xMin() > sV[k]->yMax() - sV[k]->yMin());
        is_v[k]
            = (sV[k]->xMax() - sV[k]->xMin() < sV[k]->yMax() - sV[k]->yMin());
      }
    }

    int ok = 1;
    int ii = 0;
    int tx = 0;
    int ty = 0;
    int via_x;
    int via_y;

    if (nvia) {
      // move to via point, check that this does not contract shapes
      for (k = 0; k < sN; k++)
        if (sV[k]->isVia())
          break;
      tx = spV[k]->_x;
      ty = spV[k]->_y;
      // check all vias at same point
      for (++k; k < sN; k++)
        if (sV[k]->isVia()) {
          via_x = spV[k]->_x;
          via_y = spV[k]->_y;
          if (via_x != tx || via_y != ty)
            ok = 0;
        }
      if (ok) {
        for (ii = 0; ii < pN; ii++)
          if (_ptV[pS[ii]]._x == tx && _ptV[pS[ii]]._y == ty)
            break;
        if (ii == pN)
          ok = 0;
      }
      // check all wires collinear with via point
      for (k = 0; ok && k < sN; k++) {
        if (is_h[k]) {
          if (ty + ty != sV[k]->yMin() + sV[k]->yMax())
            ok = 0;
        } else {
          if (tx + tx != sV[k]->xMin() + sV[k]->xMax())
            ok = 0;
        }
      }
      // check that no wire extends beyond the via point in both dirs
      for (k = 0; ok && k < sN; k++)
        if (!sV[k]->isVia()) {
          if (sV[k]->xMin() < tx - w / 2 && tx + w / 2 < sV[k]->xMax())
            ok = 0;
          if (sV[k]->yMin() < ty - w / 2 && ty + w / 2 < sV[k]->yMax())
            ok = 0;
        }
      if (ok) {
        adjustCommit(&_ptV[pS[ii]], rV, spV, sN);
        continue;
      }
    }

    if (w && (ok_ver || ok_hor) && nvia <= 1) {
      // we will move all points to pS[ii],
      // choosing ii as the via point,
      // or to avoid moving a pt that matches lo or hi
      // or default to 0
      ii = -1;
      if (nvia == 1) {
        for (k = 0; k < sN; k++)
          if (sV[k]->isVia())
            break;
        via_x = spV[k]->_x;
        via_y = spV[k]->_y;
        for (ii = 0; ii < pN; ii++)
          if (_ptV[pS[ii]]._x == via_x && _ptV[pS[ii]]._y == via_y)
            break;
      }
      if (ii == pN) {
      } else if (ok_ver) {
        // w = xhi-xlo;
        if (ii < 0) {
          for (ii = 0; ii < pN; ii++)
            if (_ptV[pS[ii]]._y - w / 2 == ylo
                || _ptV[pS[ii]]._y + w / 2 == yhi)
              break;
          if (ii == pN)
            ii = 0;
        }
        for (k = 0; k < pN; k++)
          if (k != ii) {
            if (_ptV[pS[k]]._y - w / 2 == ylo || _ptV[pS[k]]._y + w / 2 == yhi)
              break;
            if (_ptV[pS[k]]._x != _ptV[pS[ii]]._x)
              break;  // not expected
          }
        if (k >= pN) {
          adjustCommit(&_ptV[pS[ii]], rV, spV, sN);
          continue;
        }
      } else {
        // w = yhi-ylo;
        if (ii < 0) {
          for (ii = 0; ii < pN; ii++)
            if (_ptV[pS[ii]]._x - w / 2 == xlo
                || _ptV[pS[ii]]._x + w / 2 == xhi)
              break;
          if (ii == pN)
            ii = 0;
        }
        for (k = 0; k < pN; k++)
          if (k != ii) {
            if (_ptV[pS[k]]._x - w / 2 == xlo || _ptV[pS[k]]._x + w / 2 == xhi)
              break;
            if (_ptV[pS[k]]._y != _ptV[pS[ii]]._y)
              break;  // not expected
          }
        if (k >= pN) {
          adjustCommit(&_ptV[pS[ii]], rV, spV, sN);
          continue;
        }
      }
    }

    if (w && !ok_ver && !ok_hor) {
      // we will move all points to pS[ii],
      // choosing ii as a point that:
      //   matches y of hor wires
      //   matches x of vert wires
      //   matches x,y of all vias
      // need to check that an endcap does not get contracted
      if (nvia) {
        for (k = 0; k < sN; k++)
          if (sV[k]->isVia())
            break;
        tx = spV[k]->_x;
        ty = spV[k]->_y;
        for (++k; k < sN; k++)
          if (sV[k]->isVia()) {
            via_x = spV[k]->_x;
            via_y = spV[k]->_y;
            if (via_x != tx || via_y != ty)
              ok = 0;
          }
      } else {
        for (k = 0; k < sN; k++)
          if (is_h[k])
            break;
        if (k == sN)
          ok = 0;
        else
          ty = (sV[k]->yMin() + sV[k]->yMax()) / 2;
        for (k = 0; k < sN; k++)
          if (is_v[k])
            break;
        if (k == sN)
          ok = 0;
        else
          tx = (sV[k]->xMin() + sV[k]->xMax()) / 2;
      }
      if (ok) {
        for (ii = 0; ii < pN; ii++)
          if (_ptV[pS[ii]]._x == tx && _ptV[pS[ii]]._y == ty)
            break;
        if (ii == pN)
          ok = 0;
      }
      for (k = 0; ok && k < sN; k++) {
        if (is_h[k] && ty + ty != sV[k]->yMin() + sV[k]->yMax())
          ok = 0;
        if (is_v[k] && tx + tx != sV[k]->xMin() + sV[k]->xMax())
          ok = 0;
      }
      // check that no shape extends beyond the t point
      for (k = 0; ok && k < sN; k++) {
        if (sV[k]->xMin() < tx - w / 2 && tx + w / 2 < sV[k]->xMax())
          ok = 0;
        if (sV[k]->yMin() < ty - w / 2 && ty + w / 2 < sV[k]->yMax())
          ok = 0;
      }
      if (ok) {
        adjustCommit(&_ptV[pS[ii]], rV, spV, sN);
        continue;
      }
    }

    if (_ptV[pS[0]]._tindex >= 0) {
      // failed to adjust the shapes so far
      // Now check to see if all the points are term-shorted
      // If so, just remove the shorts.
      for (k = 1; k < pN; k++)
        if (_ptV[pS[k]]._tindex != _ptV[pS[0]]._tindex)
          break;
      if (k == pN) {
        for (k = 0; k < pN; k++)
          pgV[pS[k]].visited = 2;
        for (k = 0; k < pN; k++)
          for (e = pgV[pS[k]].edges; e; e = e->next)
            if (!e->skip && e->s && pgV[e->to].visited == 2) {
              e->skip = true;
              e->reverse->skip = true;
              e->s->_skip = true;
            }
        for (k = 0; k < pN; k++)
          pgV[pS[k]].visited = 1;
        continue;
      }
    }

    // cannot collapse to one point
    // try to patch two points
    for (k = 1; k < pN; k++)
      if (_ptV[pS[k]]._x != _ptV[pS[0]]._x || _ptV[pS[k]]._y != _ptV[pS[0]]._y)
        break;
    int kother = k;
    for (k = kother + 1; k < pN; k++) {
      if (_ptV[pS[k]]._x == _ptV[pS[0]]._x && _ptV[pS[k]]._y == _ptV[pS[0]]._y)
        continue;
      if (_ptV[pS[k]]._x == _ptV[pS[kother]]._x
          && _ptV[pS[k]]._y == _ptV[pS[kother]]._y)
        continue;
      break;
    }
    k = 0;
  }
}

void tmg_conn::adjustCommit(tmg_rcpt* p, tmg_rc** rV, tmg_rcpt** spV, int sN)
{
  int k;
  for (k = 0; k < sN; k++)
    if (spV[k] != p) {
      int dx = p->_x - spV[k]->_x;
      int dy = p->_y - spV[k]->_y;
      tmg_rcpt* p2;
      if (&_ptV[rV[k]->_ifr] == spV[k])
        p2 = &_ptV[rV[k]->_ito];
      else
        p2 = &_ptV[rV[k]->_ifr];
      if (dx) {
        if (p->_x < p2->_x) {
          rV[k]->_shape.setXmin(rV[k]->_shape.xMin() + dx);
        } else if (p->_x > p2->_x) {
          rV[k]->_shape.setXmax(rV[k]->_shape.xMax() + dx);
        }
      } else if (dy) {
        if (p->_y < p2->_y) {
          rV[k]->_shape.setYmin(rV[k]->_shape.yMin() + dy);
        } else if (p->_y > p2->_y) {
          rV[k]->_shape.setYmax(rV[k]->_shape.yMax() + dy);
        }
      }
    }
  for (k = 0; k < sN; k++)
    if (spV[k] != p) {
      spV[k]->_x = p->_x;
      spV[k]->_y = p->_y;
    }
}

int tmg_conn::getDisconnectedStart()
{
  int j;
  for (j = 0; j < _ptV.size(); j++)
    if (!_graph->_ptV[j].visited) {
      if (_graph->_ptV[j].edges && !_graph->_ptV[j].edges->next) {
        return j;
      }
    }
  for (j = 0; j < _ptV.size(); j++)
    if (!_graph->_ptV[j].visited) {
      if (_graph->_ptV[j].edges) {
        return j;
      }
    }
  return -1;
}

void tmg_conn::copyWireIdToVisitedShorts(int j)
{
  // copy _ptV[j]._dbwire_id to visited points shorted to j
  int wire_id = _ptV[j]._dbwire_id;
  tmg_rcpt* x0 = &_ptV[j];
  tmg_rcpt* x;
  for (x = x0->_sring; x && x != x0; x = x->_sring) {
    if (x->_dbwire_id < 0 && _graph->_ptV[x - &_ptV[0]].visited) {
      x->_dbwire_id = wire_id;
    }
  }
}

}  // namespace odb
