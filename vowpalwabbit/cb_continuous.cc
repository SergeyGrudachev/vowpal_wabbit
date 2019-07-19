/*
Copyright (c) by respective owners including Yahoo!, Microsoft, and
individual contributors. All rights reserved.  Released under a BSD (revised)
license as described in the file LICENSE.
 */
#include <float.h>

#include "example.h"
#include "parse_primitives.h"
#include "vw.h"
#include "vw_exception.h"
#include "cb_continuous.h"

using namespace LEARNER;
using namespace std;

namespace CB_CONT
{
char* bufread_label(CB_CONT::label* ld, char* c, io_buf& cache)
{
  size_t num = *(size_t*)c;
  ld->costs.clear();
  c += sizeof(size_t);
  size_t total = sizeof(cb_cont_class) * num;
  if (cache.buf_read(c, total) < total)
  {
    cout << "error in demarshal of cost data" << endl;
    return c;
  }
  for (size_t i = 0; i < num; i++)
  {
    cb_cont_class temp = *(cb_cont_class*)c;
    c += sizeof(cb_cont_class);
    ld->costs.push_back(temp);
  }

  return c;
}

size_t read_cached_label(shared_data*, void* v, io_buf& cache)
{
  CB_CONT::label* ld = (CB_CONT::label*)v;
  ld->costs.clear();
  char* c;
  size_t total = sizeof(size_t);
  if (cache.buf_read(c, total) < total)
    return 0;
  bufread_label(ld, c, cache);

  return total;
}

float weight(void*) { return 1.; }

char* bufcache_label(CB_CONT::label* ld, char* c)
{
  *(size_t*)c = ld->costs.size();
  c += sizeof(size_t);
  for (size_t i = 0; i < ld->costs.size(); i++)
  {
    *(cb_cont_class*)c = ld->costs[i];
    c += sizeof(cb_cont_class);
  }
  return c;
}

void cache_label(void* v, io_buf& cache)
{
  char* c;
  CB_CONT::label* ld = (CB_CONT::label*)v;
  cache.buf_write(c, sizeof(size_t) + sizeof(cb_cont_class) * ld->costs.size());
  bufcache_label(ld, c);
}

void default_label(void* v)
{
  CB_CONT::label* ld = (CB_CONT::label*)v;
  ld->costs.clear();
}

bool test_label(void* v)
{
  CB_CONT::label* ld = (CB_CONT::label*)v;
  if (ld->costs.size() == 0)
    return true;
  for (size_t i = 0; i < ld->costs.size(); i++)
    if (FLT_MAX != ld->costs[i].cost && ld->costs[i].probability > 0.)
      return false;
  return true;
}

void delete_label(void* v)
{
  CB_CONT::label* ld = (CB_CONT::label*)v;
  ld->costs.delete_v();
}

void copy_label(void* dst, void* src)
{
  CB_CONT::label* ldD = (CB_CONT::label*)dst;
  CB_CONT::label* ldS = (CB_CONT::label*)src;
  copy_array(ldD->costs, ldS->costs);
}

void parse_label(parser* p, shared_data*, void* v, v_array<substring>& words)
{
  CB_CONT::label* ld = (CB_CONT::label*)v;
  ld->costs.clear();
  for (size_t i = 0; i < words.size(); i++)
  {
    cb_cont_class f;
    tokenize(':', words[i], p->parse_name);

    if (p->parse_name.size() < 1 || p->parse_name.size() > 3)
      THROW("malformed cost specification: " << p->parse_name);

    f.partial_prediction = 0.;
    f.action = (uint32_t)hashstring(p->parse_name[0], 0);
    f.cost = FLT_MAX;

    if (p->parse_name.size() > 1)
      f.cost = float_of_substring(p->parse_name[1]);

    if (nanpattern(f.cost))
      THROW("error NaN cost (" << p->parse_name[1] << " for action: " << p->parse_name[0]);

    f.probability = .0;
    if (p->parse_name.size() > 2)
      f.probability = float_of_substring(p->parse_name[2]);

    if (nanpattern(f.probability))
      THROW("error NaN probability (" << p->parse_name[2] << " for action: " << p->parse_name[0]);

    if (f.probability > 1.0)
    {
      cerr << "invalid probability > 1 specified for an action, resetting to 1." << endl;
      f.probability = 1.0;
    }
    if (f.probability < 0.0)
    {
      cerr << "invalid probability < 0 specified for an action, resetting to 0." << endl;
      f.probability = .0;
    }
    if (substring_equal(p->parse_name[0], "shared"))
    {
      if (p->parse_name.size() == 1)
      {
        f.probability = -1.f;
      }
      else
        cerr << "shared feature vectors should not have costs" << endl;
    }

    ld->costs.push_back(f);
  }
}

label_parser cb_cont_label = {default_label, parse_label, cache_label, read_cached_label, delete_label, weight, copy_label,
    test_label, sizeof(label)};

bool ec_is_example_header(example& ec)  // example headers just have "shared"
{
  v_array<CB_CONT::cb_cont_class> costs = ec.l.cb_cont.costs;
  if (costs.size() != 1)
    return false;
  if (costs[0].probability == -1.f)
    return true;
  return false;
}

void print_update(vw& all, bool is_test, example& ec, multi_ex* ec_seq, bool action_scores)
{
  if (all.sd->weighted_examples() >= all.sd->dump_interval && !all.quiet && !all.bfgs)
  {
    size_t num_features = ec.num_features;

    size_t pred = ec.pred.multiclass; // TODO: what is pred here? the same?
    if (ec_seq != nullptr)
    {
      num_features = 0;
      // TODO: code duplication csoaa.cc LabelDict::ec_is_example_header
      for (size_t i = 0; i < (*ec_seq).size(); i++)
        if (!CB_CONT::ec_is_example_header(*(*ec_seq)[i]))
          num_features += (*ec_seq)[i]->num_features;
    }
    std::string label_buf;
    if (is_test)
      label_buf = " unknown";
    else
      label_buf = " known";

    if (action_scores)
    {
      std::ostringstream pred_buf;
      pred_buf << std::setw(all.sd->col_current_predict) << std::right << std::setfill(' ');
      if (ec.pred.p_d.size() > 0)
        pred_buf << ec.pred.p_d[0].action << ":" << ec.pred.p_d[0].value << "..."; //TODO: changed a_s to p_d, correct?!
      else
        pred_buf << "no action";
      all.sd->print_update(all.holdout_set_off, all.current_pass, label_buf, pred_buf.str(), num_features,
          all.progress_add, all.progress_arg);
    }
    else
      all.sd->print_update(all.holdout_set_off, all.current_pass, label_buf, (uint32_t)pred, num_features,
          all.progress_add, all.progress_arg);
  }
}
}  // namespace CB_CONT

namespace CB_CONT_EVAL
{
size_t read_cached_label(shared_data* sd, void* v, io_buf& cache)
{
  CB_CONT_EVAL::label* ld = (CB_CONT_EVAL::label*)v;
  char* c;
  size_t total = sizeof(uint32_t);
  if (cache.buf_read(c, total) < total)
    return 0;
  ld->action = *(uint32_t*)c;

  return total + CB_CONT::read_cached_label(sd, &(ld->event), cache);
}

void cache_label(void* v, io_buf& cache)
{
  char* c;
  CB_CONT_EVAL::label* ld = (CB_CONT_EVAL::label*)v;
  cache.buf_write(c, sizeof(uint32_t));
  *(uint32_t*)c = ld->action;

  CB_CONT::cache_label(&(ld->event), cache);
}

void default_label(void* v)
{
  CB_CONT_EVAL::label* ld = (CB_CONT_EVAL::label*)v;
  CB_CONT::default_label(&(ld->event));
  ld->action = 0;
}

bool test_label(void* v)
{
  CB_CONT_EVAL::label* ld = (CB_CONT_EVAL::label*)v;
  return CB_CONT::test_label(&ld->event);
}

void delete_label(void* v)
{
  CB_CONT_EVAL::label* ld = (CB_CONT_EVAL::label*)v;
  CB_CONT::delete_label(&(ld->event));
}

void copy_label(void* dst, void* src)
{
  CB_CONT_EVAL::label* ldD = (CB_CONT_EVAL::label*)dst;
  CB_CONT_EVAL::label* ldS = (CB_CONT_EVAL::label*)src;
  CB_CONT::copy_label(&(ldD->event), &(ldS)->event);
  ldD->action = ldS->action;
}

void parse_label(parser* p, shared_data* sd, void* v, v_array<substring>& words)
{
  CB_CONT_EVAL::label* ld = (CB_CONT_EVAL::label*)v;

  if (words.size() < 2)
    THROW("Evaluation can not happen without an action and an exploration");

  ld->action = (uint32_t)hashstring(words[0], 0);

  words.begin()++;

  CB_CONT::parse_label(p, sd, &(ld->event), words);

  words.begin()--;
}

label_parser cb_cont_eval = {default_label, parse_label, cache_label, read_cached_label, delete_label, CB_CONT::weight,
    copy_label, test_label, sizeof(CB_CONT_EVAL::label)};
}  // namespace CB_CONT_EVAL
