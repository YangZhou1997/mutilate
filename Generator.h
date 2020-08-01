// -*- c++ -*-

// 1. implement "fixed" generator
// 2. implement discrete generator
// 3. implement combine generator? 

#ifndef GENERATOR_H
#define GENERATOR_H

#define MAX(a,b) ((a) > (b) ? (a) : (b))

// #include "config.h"

#include <string>
#include <vector>
#include <utility>

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "util.h"

// Generator syntax:
//
// \d+ == fixed
// n[ormal]:mean,sd
// e[xponential]:lambda
// p[areto]:scale,shape
// g[ev]:loc,scale,shape
// fb_value, fb_key, fb_rate

class Generator {
public:
  Generator() {}
  //  Generator(const Generator &g) = delete;
  //  virtual Generator& operator=(const Generator &g) = delete;
  virtual ~Generator() {}

  virtual double generate(double U = -1.0) = 0;
  virtual void set_lambda(double lambda) {DIE("set_lambda() not implemented");}
protected:
  std::string type;
};

class Fixed : public Generator {
public:
  Fixed(double _value = 1.0) : value(_value) { D("Fixed(%f)", value); }
  virtual double generate(double U = -1.0) { return value; }
  virtual void set_lambda(double lambda) {
    if (lambda > 0.0) value = 1.0 / lambda;
    else value = 0.0;
  }

private:
  double value;
};

class Uniform : public Generator {
public:
  Uniform(double _scale) : scale(_scale) { D("Uniform(%f)", scale); }

  virtual double generate(double U = -1.0) {
    if (U < 0.0) U = drand48();
    return scale * U;
  }

  virtual void set_lambda(double lambda) {
    if (lambda > 0.0) scale = 2.0 / lambda;
    else scale = 0.0;
  }

private:
  double scale;
};

class Normal : public Generator {
public:
  Normal(double _mean = 1.0, double _sd = 1.0) : mean(_mean), sd(_sd) {
    D("Normal(mean=%f, sd=%f)", mean, sd);
  }

  virtual double generate(double U = -1.0) {
    if (U < 0.0) U = drand48();
    double V = U; // drand48();
    double N = sqrt(-2 * log(U)) * cos(2 * M_PI * V);
    return mean + sd * N;
  }

  virtual void set_lambda(double lambda) {
    if (lambda > 0.0) mean = 1.0 / lambda;
    else mean = 0.0;
  }

private:
  double mean, sd;
};

class Exponential : public Generator {
public:
  Exponential(double _lambda = 1.0) : lambda(_lambda) {
    D("Exponential(lambda=%f)", lambda);
  }

  virtual double generate(double U = -1.0) {
    if (lambda <= 0.0) return 0.0;
    if (U < 0.0) U = drand48();
    return -log(U) / lambda;
  }

  virtual void set_lambda(double lambda) { this->lambda = lambda; }

private:
  double lambda;
};

class GPareto : public Generator {
public:
  GPareto(double _loc = 0.0, double _scale = 1.0, double _shape = 1.0) :
    loc(_loc), scale(_scale), shape(_shape) {
    assert(shape != 0.0);
    D("GPareto(loc=%f, scale=%f, shape=%f)", loc, scale, shape);
  }

  virtual double generate(double U = -1.0) {
    if (U < 0.0) U = drand48();
    return loc + scale * (pow(U, -shape) - 1) / shape;
  }

  virtual void set_lambda(double lambda) {
    if (lambda <= 0.0) scale = 0.0;
    else scale = (1 - shape) / lambda - (1 - shape) * loc;
  }

private:
  double loc /* mu */;
  double scale /* sigma */, shape /* k */;
};

class GEV : public Generator {
public:
  GEV(double _loc = 0.0, double _scale = 1.0, double _shape = 1.0) :
    e(1.0), loc(_loc), scale(_scale), shape(_shape) {
    assert(shape != 0.0);
    D("GEV(loc=%f, scale=%f, shape=%f)", loc, scale, shape);
  }

  virtual double generate(double U = -1.0) {
    return loc + scale * (pow(e.generate(U), -shape) - 1) / shape;
  }

private:
  Exponential e;
  double loc /* mu */, scale /* sigma */, shape /* k */;
};

class Discrete : public Generator {
public:
  ~Discrete() { delete def; }
  Discrete(Generator* _def = NULL) : def(_def) {
    if (def == NULL) def = new Fixed(0.0);
  }

  virtual double generate(double U = -1.0) {
    double Uc = U;
    if (pv.size() > 0 && U < 0.0) U = drand48();

    double sum = 0;
 
    for (auto p: pv) {
      sum += p.first;
      if (U < sum) return p.second;
    }

    return def->generate(Uc);
  }

  void add(double p, double v) {
    pv.push_back(std::pair<double,double>(p, v));
  }

private:
  Generator *def;
  std::vector< std::pair<double,double> > pv;
};

// generate zipf integer given range_n and skew (theta)
class ZipfGen{
    uint64_t range_n;
    double alpha, zetan, eta, skew;
    double zeta(uint64_t n, double theta){
        double ret = 0;
        for (uint64_t i = 0; i < n; i++){
            ret += pow(1.0 / (double) (i + 1), theta);
        }
        return ret;
    }
public: 
    ZipfGen(uint64_t n, double theta){
        range_n = n;
        skew = theta;
        alpha = 1 / (1 - theta);
        zetan = zeta(n, theta);
        eta = (1 - pow(2.0 / n, 1 - theta)) / (1 - zeta(2, theta) / zetan);
    }
    // return [0 , range_n - 1)
    uint64_t zipf_next(){
        double u = rand() * 1.0 / RAND_MAX;
        double uz = u * zetan;
        uint64_t val;
        if (uz < 1) 
            val = 1;
        else if (uz < 1 + pow(0.5, skew)) 
            val = 2;
        else
            val = 1 + (uint64_t)(range_n * pow(eta*u - eta + 1.0, alpha));
        val--;
        return val % range_n; 
    }
};

class KeyGenerator {
public:
  KeyGenerator(Generator* _g, double _max=10000, double _skew=0.99) : g(_g), max(_max), skew(_skew) {
      ig = new ZipfGen(_max, _skew);
  }
// ind will determine both key and key len; 
  std::string generate(uint64_t ind) {
    uint64_t h = fnv_64(ind);
    double U = (double) h / ULLONG_MAX;
    double G = g->generate(U);
    int keylen = MAX(round(G), floor(log10(max)) + 1);
    char key[256];
    snprintf(key, 256, "%0*" PRIu64, keylen, ind);

    //    D("%d = %s", ind, key);
    return std::string(key);
  }
  std::string generate_zipf() {
      uint64_t ind = ig->zipf_next();
      return generate(ind);
  }
private:
  Generator* g;
  ZipfGen* ig;
  double max;
  double skew;
};

class ValueGenerator {
public:
  ValueGenerator(Generator* _g) : g(_g) {
      init_random_stuff();
  }
// mimicing Connection::issue_something;
  std::string generate() {
    double G = g->generate();
    int valuelen = round(G);
    int index = lrand48() % (1024 * 1024);
    memcpy(value, &random_char[index], valuelen);

    //    D("%d = %s", ind, key);
    return std::string(value, valuelen);
  }
  void init_random_stuff() {
	static char lorem[] =
	R"(Lorem ipsum dolor sit amet, consectetur adipiscing elit. Maecenas turpis dui, suscipit non vehicula non, malesuada id sem. Phasellus suscipit nisl ut dui consectetur ultrices tincidunt eros aliquet. Donec feugiat lectus sed nibh ultrices ultrices. Vestibulum ante ipsum primis in faucibus orci luctus et ultrices posuere cubilia Curae; Mauris suscipit eros sed justo lobortis at ultrices lacus molestie. Duis in diam mi. Cum sociis natoque penatibus et magnis dis parturient montes, nascetur ridiculus mus. Ut cursus viverra sagittis. Vivamus non facilisis tortor. Integer lectus arcu, sagittis et eleifend rutrum, condimentum eget sem. Vestibulum tempus tellus non risus semper semper. Morbi molestie rhoncus mi, in egestas dui facilisis et.)";

	size_t cursor = 0;
	while (cursor < sizeof(random_char)) {
		size_t max = sizeof(lorem);
		if (sizeof(random_char) - cursor < max)
			max = sizeof(random_char) - cursor;

		memcpy(&random_char[cursor], lorem, max);
		cursor += max;
	}
   }
private:
    Generator* g;
    char random_char[2 * 1024 * 1024];  // Buffer used to generate random values.
    char value[1024 * 1024];
};

Generator* createGenerator(std::string str);
Generator* createFacebookKey();
Generator* createFacebookValue();
Generator* createFacebookIA();

#endif // GENERATOR_H
