# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-pedantic"
# define NPY_NO_DEPRECATED_API NPY_1_8_API_VERSION
# include <Python.h>
# include <numpy/arrayobject.h>
# pragma GCC diagnostic pop
#include <structmember.h>

# include <math.h>
# include <assert.h>
# include <stdbool.h>
# include <stdio.h>

# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wunused-parameter"

# define TYPE_INDEX NPY_DOUBLE

typedef struct anchor
{
	double time;
	double state[{{n}}];
	double diff[{{n}}];
	struct anchor * next;
	struct anchor * previous;
	{% if n_basic != n: %}
	double sp_matrix[4][4];
	{% endif %}
} anchor;

typedef struct
{
	PyObject_HEAD
	anchor * current;
	anchor * first_anchor;
	anchor * last_anchor;
	{% if anchor_mem_length: %}
	anchor ** anchor_mem;
	anchor ** current_anchor;
	{% endif %}
	double past_within_step;
	double old_new_y[{{n}}];
	double error[{{n}}];
	double last_actual_step_start;
} dde_integrator;

void append_anchor(
	dde_integrator * const self,
	double const time,
	double state[{{n}}],
	double diff[{{n}}])
{
	anchor * new_anchor = malloc(sizeof(anchor));
	assert(new_anchor!=NULL);
	new_anchor->time = time;
	memcpy(new_anchor->state, state, {{n}}*sizeof(double));
	memcpy(new_anchor->diff, diff, {{n}}*sizeof(double));
	new_anchor->next = NULL;
	new_anchor->previous = self->last_anchor;
	
	if (self->last_anchor)
	{
		assert(self->last_anchor->next==NULL);
		self->last_anchor->next = new_anchor;
	}
	else
	{
		assert(self->first_anchor==NULL);
		self->first_anchor = new_anchor;
	}
	
	self->last_anchor = new_anchor;
}

void remove_first_anchor(dde_integrator * const self)
{
	anchor * old_first_anchor = self->first_anchor;
	
	self->first_anchor = old_first_anchor->next;
	if (self->first_anchor)
		self->first_anchor->previous = NULL;
	else
		self->last_anchor = NULL;
	
	free(old_first_anchor);
}

void replace_last_anchor(
	dde_integrator * const self,
	double const time,
	double state[{{n}}],
	double diff[{{n}}]
)
{
	memcpy(self->old_new_y , self->last_anchor->state, {{n}}*sizeof(double));
	self->last_anchor->time = time;
	memcpy(self->last_anchor->state, state, {{n}}*sizeof(double));
	memcpy(self->last_anchor->diff, diff, {{n}}*sizeof(double));
}

static PyObject * get_t(dde_integrator const * const self)
{
	return PyFloat_FromDouble(self->current->time);
}

{% if anchor_mem_length: %}
anchor get_past_anchors(dde_integrator * const self, double const t)
{
	anchor * ca = *(self->current_anchor);
	
	while ( (ca->time > t) && (ca->previous) )
		ca = ca->previous;
	
	assert(ca->next != NULL);
	
	while ( (ca->next->time < t) && (ca->next->next) )
		ca = ca->next;
	
	if (t > self->current->time)
		self->past_within_step = fmax(self->past_within_step,t-self->current->time);
	
	*(self->current_anchor) = ca;
	self->current_anchor++;
	return *ca;
}
{% endif %}


double get_past_value(
	dde_integrator const * const self,
	double const t,
	unsigned int const index,
	anchor const v)
{
	anchor const w = *(v.next);
	double const q = w.time-v.time;
	double const x = (t - v.time)/q;
	double const a = v.state[index];
	double const b = v.diff[index] * q;
	double const c = w.state[index];
	double const d = w.diff[index] * q;
	
	return (1-x) * ( (1-x) * (b*x + (a-c)*(2*x+1)) - d*x*x) + c;
}

static PyObject * get_recent_state(dde_integrator const * const self, PyObject * args)
{
	assert(self->last_anchor);
	assert(self->first_anchor);

	double t;
	if (!PyArg_ParseTuple(args, "d", &t))
	{
		PyErr_SetString(PyExc_ValueError,"Wrong input.");
		return NULL;
	}
	
	npy_intp dims[1] = { {{n}} };
	# pragma GCC diagnostic push
	# pragma GCC diagnostic ignored "-pedantic"
	PyArrayObject * result = (PyArrayObject *)PyArray_SimpleNew(1, dims, TYPE_INDEX);
	# pragma GCC diagnostic pop
	
	anchor const w = *(self->last_anchor);
	anchor const v = *(w.previous);
	double const q = w.time-v.time;
	double const x = (t - v.time)/q;
	
	for (int index=0; index<{{n}}; index++)
	{
		double const a = v.state[index];
		double const b = v.diff[index] * q;
		double const c = w.state[index];
		double const d = w.diff[index] * q;
	
		* (double *) PyArray_GETPTR1(result, index) = 
				(1-x) * ( (1-x) * (b*x + (a-c)*(2*x+1)) - d*x*x) + c;
	}
	
	# pragma GCC diagnostic push
	# pragma GCC diagnostic ignored "-pedantic"
	return (PyObject *) result;
	# pragma GCC diagnostic pop
}

static PyObject * get_current_state(dde_integrator const * const self)
{
	assert(self->last_anchor);
	
	npy_intp dims[1] = { {{n}} };
	# pragma GCC diagnostic push
	# pragma GCC diagnostic ignored "-pedantic"
	PyArrayObject * result = (PyArrayObject *)PyArray_SimpleNew(1, dims, TYPE_INDEX);
	# pragma GCC diagnostic pop
	
	for (int index=0; index<{{n}}; index++)
		* (double *) PyArray_GETPTR1(result, index) = self->last_anchor->state[index];
	
	# pragma GCC diagnostic push
	# pragma GCC diagnostic ignored "-pedantic"
	return (PyObject *) result;
	# pragma GCC diagnostic pop
}

# define set_dy(i, value) (dY[i] = value)
# define current_y(i) (y[i])
# define past_y(t, i, anchor) (get_past_value(self, t, i, anchor))
# define anchors(t) (get_past_anchors(self, t))

# define get_f_helper(i) ((f_helper[i]))
# define set_f_helper(i,value) (f_helper[i] = value)
# define get_f_anchor_helper(i) ((f_anchor_helper[i]))
# define set_f_anchor_helper(i,value) (f_anchor_helper[i] = value)


{% if has_any_helpers: %}
# include "helpers_definitions.c"
{% endif %}
# include "f_definitions.c"
static PyObject * eval_f(
	dde_integrator * const self,
	double const t,
	double y[{{n}}],
	double dY[{{n}}])
{
	{% if anchor_mem_length: %}
		self->current_anchor = self->anchor_mem;
	{% endif %}
	
	{% if number_of_helpers>0: %}
	double f_helper[{{number_of_helpers}}];
	{% endif %}
	{% if number_of_anchor_helpers>0: %}
	anchor f_anchor_helper[{{number_of_anchor_helpers}}];
	{% endif %}
	
	{% if has_any_helpers>0: %}
	# include "helpers.c"
	{% endif %}
	# include "f.c"
	
	Py_RETURN_NONE;
}

static PyObject * get_next_step(dde_integrator * const self, PyObject * args)
{
	assert(self->last_anchor);
	assert(self->first_anchor);
	
	double delta_t;
	if (!PyArg_ParseTuple(args, "d", &delta_t))
	{
		PyErr_SetString(PyExc_ValueError,"Wrong input.");
		return NULL;
	}
	
	self->last_actual_step_start = self->current->time;
	
	self->past_within_step = 0.0;
	# define k_1 self->current->diff
	double argument[{{n}}];
	
	double k_2[{{n}}];
	for (int i=0; i<{{n}}; i++)
		argument[i] = self->current->state[i] + 0.5*delta_t*k_1[i];
	eval_f(self, self->current->time+0.5*delta_t, argument, k_2);
	
	double k_3[{{n}}];
	for (int i=0; i<{{n}}; i++)
		argument[i] = self->current->state[i] + 0.75*delta_t*k_2[i];
	eval_f(self, self->current->time+0.75*delta_t, argument, k_3);
	
	double new_y[{{n}}];
	for (int i=0; i<{{n}}; i++)
		new_y[i] = self->current->state[i] + (delta_t/9.) * (2*k_1[i]+3*k_2[i]+4*k_3[i]);
	
	double new_t = self->current->time + delta_t;
	
	double k_4[{{n}}];
	# define new_diff k_4
	eval_f(self, new_t, new_y, new_diff);
	
	for (int i=0; i<{{n}}; i++)
		self->error[i] = (5*k_1[i]-6*k_2[i]-8*k_3[i]+9*k_4[i]) * (1/72.);
	
	if (self->last_anchor == self->current)
		append_anchor(self, new_t, new_y, new_diff);
	else
		replace_last_anchor(self, new_t, new_y, new_diff);
	
	assert(self->first_anchor);
	assert(self->last_anchor);
	Py_RETURN_NONE;
}

static PyObject * get_p(dde_integrator const * const self, PyObject * args)
{
	double atol;
	double rtol;
	if (!PyArg_ParseTuple(args, "dd", &atol, &rtol))
	{
		PyErr_SetString(PyExc_ValueError,"Wrong input.");
		return NULL;
	}
	
	double p=0.0;
	for (int i=0; i<{{n}}; i++)
	{
		double x = fabs(self->error[i]/(atol+rtol*fabs(self->last_anchor->state[i])));
		if (x>p)
			p = x;
	}
	
	return PyFloat_FromDouble(p);
}

// result = np.max(np.abs(self.error)/(atol + rtol*np.abs(self.past[-1][1])))

static PyObject * check_new_y_diff(dde_integrator const * const self, PyObject * args)
{
	double atol;
	double rtol;
	if (!PyArg_ParseTuple(args, "dd", &atol, &rtol))
	{
		PyErr_SetString(PyExc_ValueError,"Wrong input.");
		return NULL;
	}
	
	bool result = true;
	for (int i=0; i<{{n}}; i++)
	{
		double difference = fabs(self->last_anchor->state[i] - self->old_new_y[i]);
		double tolerance = atol + fabs(rtol*self->last_anchor->state[i]);
		result &= (tolerance >= difference);
	}
	
	return PyBool_FromLong(result);
}

static PyObject * accept_step(dde_integrator * const self)
{
	self->current = self->last_anchor;
	Py_RETURN_NONE;
}

static PyObject * forget(dde_integrator * const self, PyObject * args)
{
	double delay;
	if (!PyArg_ParseTuple(args, "d", &delay))
	{
		PyErr_SetString(PyExc_ValueError,"Wrong input.");
		return NULL;
	}
	
	double threshold = fmin(
		self->current->time - delay,
		self->last_actual_step_start
		);
	assert(self->first_anchor != self->last_anchor);
	while (self->first_anchor->next->time < threshold)
	{
		{% if anchor_mem_length: %}
		# ifndef NDEBUG
		for (int i=0; i<{{anchor_mem_length}}; i++)
			assert(self->anchor_mem[i] != self->first_anchor);
		# endif
		{% endif %}
		remove_first_anchor(self);
	}
	
	assert(self->first_anchor != self->last_anchor);
	
	Py_RETURN_NONE;
}

static void dde_integrator_dealloc(dde_integrator * const self)
{
	while (self->first_anchor)
		remove_first_anchor(self);
	{% if anchor_mem_length: %}
	free(self->anchor_mem);
	{% endif %}
	
	Py_TYPE(self)->tp_free((PyObject *)self);
}

static int initiate_past_from_list(dde_integrator * const self, PyObject * const past)
{
	for (Py_ssize_t i=0; i<PyList_Size(past); i++)
	{
		PyObject * pyanchor = PyList_GetItem(past,i);
		double time;
		PyArrayObject * pystate;
		PyArrayObject * pydiff;
		if (!PyArg_ParseTuple(pyanchor, "dO!O!", &time, &PyArray_Type, &pystate, &PyArray_Type, &pydiff))
		{
			PyErr_SetString(PyExc_ValueError,"Wrong input.");
			return 0;
		}
		
		double state[{{n}}];
		for (int i=0; i<{{n}}; i++)
			state[i] = * (double *) PyArray_GETPTR1(pystate,i);
		
		double diff[{{n}}];
		for (int i=0; i<{{n}}; i++)
			diff[i] = * (double *) PyArray_GETPTR1(pydiff,i);
		
		append_anchor(self, time, state, diff);
	}
	
	return 1;
}

static int dde_integrator_init(dde_integrator * self, PyObject * args)
{
	PyObject * past;
	if (!PyArg_ParseTuple(args, "O!", &PyList_Type, &past))
	{
		PyErr_SetString(PyExc_ValueError,"Wrong input.");
		return -1;
	}
	
	self->first_anchor = NULL;
	self->last_anchor = NULL;
	
	if (!initiate_past_from_list(self, past))
		return -1;
	self->current = self->last_anchor;
	assert(self->first_anchor != self->last_anchor);
	assert(self->first_anchor != NULL);
	assert(self->last_anchor != NULL);
	self->last_actual_step_start = self->first_anchor->time;
	
	{% if anchor_mem_length: %}
	self->anchor_mem = malloc({{anchor_mem_length}}*sizeof(anchor *));
	assert(self->anchor_mem != NULL);
	for (int i=0; i<{{anchor_mem_length}}; i++)
		self->anchor_mem[i] = self->last_anchor->previous;
	{% endif %}
	
	return 0;
}

// Functions for Lyapunov exponents
{% if n_basic != n: %}

void calculate_sp_matrix(
	dde_integrator const * const self,
	anchor * v
)
{
	anchor const * const w = v->next;
	double const q = w->time - v->time;
	
	double cq = q/420.;
	double cqq = cq*q;
	double cqqq = cqq*q;
	memcpy(v->sp_matrix, (double[4][4]){  
		{ 156*cq  ,22*cqq  , 54*cq  ,-13*cqq  },
		{  22*cqq , 4*cqqq , 13*cqq , -3*cqqq },
		{  54*cq  ,13*cqq  ,156*cq  ,-22*cqq  },
		{ -13*cqq ,-3*cqqq ,-22*cqq ,  4*cqqq }
	}, sizeof(double[4][4]));
}

void calculate_partial_sp_matrix(
	dde_integrator const * const self,
	anchor * v,
	double const threshold
)
{
	anchor const * const w = v->next;
	double const q = w->time - v->time;
	double const z = (threshold - w->time) / q;
	
	double cq = q/420.;
	double cqq = cq*q;
	double cqqq = cqq*q;
	
	double z3 = z*z*z;
	double z4 = z*z3;
	double z5 = z*z4;
	
	double const h_1 = (- 120*z*z - 350*z - 252) * z5 * cqq ;
	double const h_2 = (-  60*z*z - 140*z -  84) * z5 * cqqq;
	double const h_3 = (- 120*z*z - 420*z - 378) * z5 * cq  ;
	double const h_4 = (-  70*z*z - 168*z - 105) * z4 * cqqq;
	double const h_6 = (          - 105*z - 140) * z3 * cqq ;
	double const h_7 = (          - 210*z - 420) * z3 * cq  ;
	double const h_5 = (2*h_2 + 3*h_4)/q;
	double const h_8 = - h_5 + h_7*q - h_6 - 0.5*(z*q)*(z*q);
	
	memcpy(v->sp_matrix, (double[4][4]){  
		{  2*h_3   , h_1    , h_7-2*h_3      , h_5                },
		{    h_1   , h_2    , h_6-h_1        , h_2+h_4            },
		{ h_7-2*h_3, h_6-h_1, 2*h_3-2*h_7-z*q, h_8                },
		{   h_5    , h_2+h_4, h_8            , h_2+(h_5+h_6-h_1)*q}
	}, sizeof(double[4][4]));
}

void calculate_sp_matrices(dde_integrator const * const self, double const delay)
{
	double const threshold = self->current->time - delay;
	
	anchor * ca = self->first_anchor;
	for (; ca->next->time<threshold; ca=ca->next)
		for (int i=0; i<4; i++)
			for (int j=0; j<4; j++)
				ca->sp_matrix[i][j] = 0.0;
	
	calculate_partial_sp_matrix(self, ca, threshold);
	ca = ca->next;
	
	for(; ca->next; ca=ca->next)
		calculate_sp_matrix(self, ca);
}

double norm_sq_interval(anchor const v, unsigned int const begin)
{
	anchor const w = *(v.next);
	double const * const vector[4] = {
				&(v.state[begin]), // a
				&(v.diff [begin]), // b/q
				&(w.state[begin]), // c
				&(w.diff [begin])  // d/q
			};
	
	double sum = 0;
	
	for (unsigned int i=0; i<4; i++)
		for (unsigned int j=0; j<4; j++)
			for (unsigned int index=0; index<{{n_basic}}; index++)
				sum += v.sp_matrix[i][j] * vector[i][index] * vector[j][index];
	
	return sum;
}

double norm_sq(dde_integrator const * const self, unsigned int const begin)
{
	double sum = 0;
	for (anchor * ca = self->first_anchor; ca->next; ca = ca->next)
		sum += norm_sq_interval(*ca, begin);
	
	return sum;
}

double scalar_product_interval(
	anchor const v,
	unsigned int const begin_1,
	unsigned int const begin_2)
{
	anchor const w = *(v.next);
	double const * const vector_1[4] = {
				&(v.state[begin_1]), // a_1
				&(v.diff [begin_1]), // b_1/q
				&(w.state[begin_1]), // c_1
				&(w.diff [begin_1])  // d_1/q
			};
	
	double const * const vector_2[4] = {
				&(v.state[begin_2]), // a_2
				&(v.diff [begin_2]), // b_2/q
				&(w.state[begin_2]), // c_2
				&(w.diff [begin_2])  // d_2/q
			};
	
	double sum = 0;
	
	for (unsigned int i=0; i<4; i++)
		for (unsigned int j=0; j<4; j++)
			for (unsigned int index=0; index<{{n_basic}}; index++)
				sum += v.sp_matrix[i][j] * vector_1[i][index] * vector_2[j][index];
	
	return sum;
}

double scalar_product(
	dde_integrator const * const self,
	unsigned int const begin_1,
	unsigned int const begin_2)
{
	double sum = 0;
	for (anchor * ca = self->first_anchor; ca->next; ca = ca->next)
		sum += scalar_product_interval(*ca, begin_1, begin_2);
	
	return sum;
}

void scale_past(
	dde_integrator const * const self,
	unsigned int const begin,
	double const factor)
{
	for (anchor * ca = self->first_anchor; ca; ca = ca->next)
		for (unsigned int i=0; i<{{n_basic}}; i++)
		{
			ca->state[begin+i] *= factor;
			ca->diff [begin+i] *= factor;
		}
}

void subtract_from_past(
	dde_integrator const * const self,
	unsigned int const begin_1,
	unsigned int const begin_2,
	double const factor)
{
	for (anchor * ca = self->first_anchor; ca; ca = ca->next)
		for (unsigned int i=0; i<{{n_basic}}; i++)
		{
			ca->state[begin_1+i] -= factor*ca->state[begin_2+i];
			ca->diff [begin_1+i] -= factor*ca->diff [begin_2+i];
		}
}

static PyObject * orthonormalise(dde_integrator const * const self, PyObject * args)
{
	assert(self->last_anchor);
	assert(self->first_anchor);
	
	unsigned int n_lyap;
	double delay;
	if (!PyArg_ParseTuple(args, "Id", &n_lyap, &delay))
	{
		PyErr_SetString(PyExc_ValueError,"Wrong input.");
		return NULL;
	}
	
	calculate_sp_matrices(self, delay);
	
	npy_intp dims[1] = { n_lyap };
	# pragma GCC diagnostic push
	# pragma GCC diagnostic ignored "-pedantic"
	PyArrayObject * norms = (PyArrayObject *)PyArray_SimpleNew(1, dims, TYPE_INDEX);
	# pragma GCC diagnostic pop
	
	for (unsigned int i=0; i<n_lyap; i++)
	{
		for (unsigned int j=0; j<i; j++)
		{
			double sp = scalar_product(self, (i+1)*{{n_basic}}, (j+1)*{{n_basic}});
			subtract_from_past(self, (i+1)*{{n_basic}}, (j+1)*{{n_basic}}, sp);
		}
		double norm = sqrt(norm_sq(self, (i+1)*{{n_basic}}));
		scale_past(self, (i+1)*{{n_basic}}, 1./norm);
		* (double *) PyArray_GETPTR1(norms, i) = norm;
	}
	
	# pragma GCC diagnostic push
	# pragma GCC diagnostic ignored "-pedantic"
	return (PyObject *) norms;
	# pragma GCC diagnostic pop
}

{% endif %}

// ======================================================

static PyMemberDef dde_integrator_members[] = {
 	{"past_within_step", T_DOUBLE, offsetof(dde_integrator, past_within_step), 0, "past_within_step"},
	{NULL}  /* Sentinel */
};

static PyMethodDef dde_integrator_methods[] = {
	{"get_t", (PyCFunction) get_t, METH_NOARGS, NULL},
	{"get_recent_state", (PyCFunction) get_recent_state, METH_VARARGS, NULL},
	{"get_current_state", (PyCFunction) get_current_state, METH_NOARGS, NULL},
	{"get_next_step", (PyCFunction) get_next_step, METH_VARARGS, NULL},
	{"get_p", (PyCFunction) get_p, METH_VARARGS, NULL},
	{"check_new_y_diff", (PyCFunction) check_new_y_diff, METH_VARARGS, NULL},
	{"accept_step", (PyCFunction) accept_step, METH_NOARGS, NULL},
	{"forget", (PyCFunction) forget, METH_VARARGS, NULL},
	{% if n_basic != n: %}
	{"orthonormalise", (PyCFunction) orthonormalise, METH_VARARGS, NULL},
	{% endif %}
	{NULL, NULL, 0, NULL}
};


static PyTypeObject dde_integrator_type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	"_jitced.dde_integrator",
	sizeof(dde_integrator), 
	0,                         // tp_itemsize 
	(destructor) dde_integrator_dealloc,
	0,                         // tp_print 
	0,0,0,0,0,0,0,0,0,0,0,0,   // ... 
	0,                         // tp_as_buffer 
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
	0,                         // tp_doc 
	0,0,0,0,0,                 // ... 
	0,                         // tp_iternext 
	dde_integrator_methods,
	dde_integrator_members,
	0,                         // tp_getset 
	0,0,0,0,                   // ...
	0,                         // tp_dictoffset 
	(initproc) dde_integrator_init,
	0,                         // tp_alloc 
	0                          // tp_new
};

static PyMethodDef {{module_name}}_methods[] = {
{NULL, NULL, 0, NULL}
};


{% if Python_version==3: %}

static struct PyModuleDef moduledef =
{
        PyModuleDef_HEAD_INIT,
        "{{module_name}}",
        NULL,
        -1,
        {{module_name}}_methods,
        NULL,
        NULL,
        NULL,
        NULL
};

PyMODINIT_FUNC PyInit_{{module_name}}(void)
{
	dde_integrator_type.tp_new = PyType_GenericNew;
	if (PyType_Ready(&dde_integrator_type) < 0)
		return NULL;
	
	PyObject * module = PyModule_Create(&moduledef);
	
	if (module == NULL)
		return NULL;
	
	Py_INCREF(&dde_integrator_type);
	PyModule_AddObject(module, "dde_integrator", (PyObject *)&dde_integrator_type);
	
	import_array();
	
	return module;
}

{% elif Python_version==2: %}

#ifndef PyMODINIT_FUNC
#define PyMODINIT_FUNC void
#endif
PyMODINIT_FUNC init{{module_name}}(void)
{
	dde_integrator_type.tp_new = PyType_GenericNew;
	if (PyType_Ready(&dde_integrator_type) < 0)
		return;
	
	PyObject * module = Py_InitModule("{{module_name}}", {{module_name}}_methods);
	
	if (module == NULL)
		return;
	
	Py_INCREF(&dde_integrator_type);
	PyModule_AddObject(module, "dde_integrator", (PyObject*) &dde_integrator_type);
	
	import_array();
}

{% endif %}
