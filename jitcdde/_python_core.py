#!/usr/bin/python3
# -*- coding: utf-8 -*-

from __future__ import print_function, division

import jitcdde._jitcdde

import sympy
import numpy as np
from itertools import count

MIN_GARBAGE = 10

sumsq = lambda x: np.sum(x**2)

sp_matrix = np.array([
	[156,  22,  54, -13],
	[ 22,   4,  13,  -3],
	[ 54,  13, 156, -22],
	[-13,  -3, -22,   4]
	])/420

def partial_sp_matrix(z):
	h_1 = - 120*z**7 - 350*z**6 - 252*z**5
	h_2 = -  60*z**7 - 140*z**6 -  84*z**5
	h_3 = - 120*z**7 - 420*z**6 - 378*z**5
	h_4 = -  70*z**6 - 168*z**5 - 105*z**4
	h_6 =            - 105*z**4 - 140*z**3
	h_7 =            - 210*z**4 - 420*z**3
	h_5 = 2*h_2 + 3*h_4
	h_8 = - h_5 + h_7 - h_6 - 210*z**2
	
	return np.array([
		[  2*h_3   , h_1    , h_7-2*h_3        , h_5              ],
		[    h_1   , h_2    , h_6-h_1          , h_2+h_4          ],
		[ h_7-2*h_3, h_6-h_1, 2*h_3-2*h_7-420*z, h_8              ],
		[   h_5    , h_2+h_4, h_8              , -h_1+h_2+h_5+h_6 ]
	])/420

def norm_sq_interval(anchors, indizes):
	q = (anchors[1][0]-anchors[0][0])
	vector = np.vstack([
	anchors[0][1][indizes],     # a
	anchors[0][2][indizes] * q, # b
	anchors[1][1][indizes],     # c
	anchors[1][2][indizes] * q, # d
	])
	
	return np.einsum(
		vector, [0,2],
		sp_matrix, [0,1],
		vector, [1,2]
		)*q

def norm_sq_partial(anchors, indizes, start):
	q = (anchors[1][0]-anchors[0][0])
	z = (start-anchors[1][0]) / q
	vector = np.vstack([
		anchors[0][1][indizes],     # a
		anchors[0][2][indizes] * q, # b
		anchors[1][1][indizes],     # c
		anchors[1][2][indizes] * q, # d
	])
	
	return np.einsum(
		vector, [0,2],
		partial_sp_matrix(z), [0,1],
		vector, [1,2]
		)*q

def scalar_product_interval(anchors, indizes_1, indizes_2):
	q = (anchors[1][0]-anchors[0][0])
	
	vector_1 = np.vstack([
		anchors[0][1][indizes_1],     # a_1
		anchors[0][2][indizes_1] * q, # b_1
		anchors[1][1][indizes_1],     # c_1
		anchors[1][2][indizes_1] * q, # d_1
	])
	
	vector_2 = np.vstack([
		anchors[0][1][indizes_2],     # a_2
		anchors[0][2][indizes_2] * q, # b_2
		anchors[1][1][indizes_2],     # c_2
		anchors[1][2][indizes_2] * q, # d_2
	])
	
	return np.einsum(
		vector_1, [0,2],
		sp_matrix, [0,1],
		vector_2, [1,2]
		)*q

def scalar_product_partial(anchors, indizes_1, indizes_2, start):
	q = (anchors[1][0]-anchors[0][0])
	z = (start-anchors[1][0]) / q
	
	vector_1 = np.vstack([
		anchors[0][1][indizes_1],     # a_1
		anchors[0][2][indizes_1] * q, # b_1
		anchors[1][1][indizes_1],     # c_1
		anchors[1][2][indizes_1] * q, # d_1
	])
	
	vector_2 = np.vstack([
		anchors[0][1][indizes_2],     # a_2
		anchors[0][2][indizes_2] * q, # b_2
		anchors[1][1][indizes_2],     # c_2
		anchors[1][2][indizes_2] * q, # d_2
	])
	
	return np.einsum(
		vector_1, [0,2],
		partial_sp_matrix(z), [0,1],
		vector_2, [1,2]
		)*q
	
	return result

class dde_integrator(object):
	def __init__(self,
				f,
				past,
				helpers = []
			):
		self.past = past
		self.t, self.y, self.diff = self.past[-1]
		self.n = len(self.y)
		self.step_count = 0 # For benchmarking purposes.
		self.last_garbage = -1
		
		t, y, current_y, past_y, anchors = jitcdde._jitcdde.provide_advanced_symbols()
		Y = sympy.symarray("Y", self.n)
		substitutions = helpers[::-1] + [(y(i),Y[i]) for i in range(self.n)]
		
		past_calls = 0
		f_wc = []
		for entry in f():
			new_entry = entry.subs(substitutions).simplify(ratio=1.0)
			past_calls += new_entry.count(anchors)
			f_wc.append(new_entry)
		
		F = self.F = sympy.lambdify(
			[t]+[Yentry for Yentry in Y],
			f_wc,
			[
				{
					anchors.__name__: self.get_past_anchors,
					past_y .__name__: self.get_past_value
				},
				"math"
			]
			)
		
		self.f = lambda T,ypsilon: np.array(F(T,*ypsilon)).flatten()
		
		self.anchor_mem = (len(past)-1)*np.ones(past_calls, dtype=int)
	
	def get_t(self):
		return self.t
	
	def get_past_anchors(self, t):
		s = self.anchor_mem[self.anchor_mem_index]
		
		if t > self.t:
			s = len(self.past)-2
			self.past_within_step = max(self.past_within_step,t-self.t)
		else:
			while self.past[s][0]>t and s>0:
				s -= 1
			while self.past[s+1][0]<t:
				s += 1
		
		self.anchor_mem[self.anchor_mem_index] = s
		self.anchor_mem_index += 1
		
		return (self.past[s], self.past[s+1])
	
	def get_past_value(self, t, i, anchors):
		q = (anchors[1][0]-anchors[0][0])
		x = (t-anchors[0][0]) / q
		a = anchors[0][1][i]
		b = anchors[0][2][i] * q
		c = anchors[1][1][i]
		d = anchors[1][2][i] * q
		
		return (1-x) * ( (1-x) * (b*x + (a-c)*(2*x+1)) - d*x**2) + c
	
	def get_recent_state(self, t):
		anchors = self.past[-2], self.past[-1]
		q = (anchors[1][0]-anchors[0][0])
		x = (t-anchors[0][0]) / q
		a = anchors[0][1]
		b = anchors[0][2] * q
		c = anchors[1][1]
		d = anchors[1][2] * q
		
		output = (1-x) * ( (1-x) * (b*x + (a-c)*(2*x+1)) - d*x**2) + c
		assert type(output) == np.ndarray
		return output
	
	def get_current_state(self):
		return self.past[-1][1]
	
	def eval_f(self, t, y):
		self.anchor_mem_index = 0
		return self.f(t, y)
	
	def get_next_step(self, delta_t):
		self.step_count += 1
		
		self.past_within_step = False
		
		k_1 = self.diff
		k_2 = self.eval_f(self.t + 0.5 *delta_t, self.y + 0.5 *delta_t*k_1)
		k_3 = self.eval_f(self.t + 0.75*delta_t, self.y + 0.75*delta_t*k_2)
		new_y = self.y + (delta_t/9.) * (2*k_1 + 3*k_2 + 4*k_3)
		new_t = self.t + delta_t 
		k_4 = new_diff = self.eval_f(new_t, new_y)
		self.error = (5*k_1 - 6*k_2 - 8*k_3 + 9*k_4) * (1/72.)
		
		if self.past[-1][0]==self.t:
			self.past.append((new_t, new_y, new_diff))
		else:
			try: self.old_new_y = self.past[-1][1]
			except AttributeError: pass
			
			self.past[-1] = (new_t, new_y, new_diff)
	
	def get_p(self, atol, rtol):
		return np.max(np.abs(self.error)/(atol + rtol*np.abs(self.past[-1][1])))
	
	def check_new_y_diff(self, atol, rtol):
		difference = np.abs(self.past[-1][1]-self.old_new_y)
		tolerance = atol + np.abs(rtol*self.past[-1][1])
		return np.all(difference <= tolerance)
	
	def accept_step(self):
		self.t, self.y, self.diff = self.past[-1]

	def forget(self, delay):
		threshold = self.t - delay
		while self.past[self.last_garbage+2][0] < threshold:
			self.last_garbage += 1
		
		if self.last_garbage >= MIN_GARBAGE:
			self.past = self.past[self.last_garbage+1:]
			self.anchor_mem -= self.last_garbage+1
			self.last_garbage = -1
	
	# ------------------------------------
	
	def norm(self, delay, indizes):
		threshold = self.t - delay
		
		i = 0
		while self.past[i+1][0] < threshold:
			i += 1
		
		# partial norm of first relevant interval
		anchors = (self.past[i],self.past[i+1])
		norm_sq = norm_sq_partial(anchors, indizes, threshold)
		
		# full norms of all others
		for i in range(i+1, len(self.past)-1):
			anchors = (self.past[i],self.past[i+1])
			norm_sq += norm_sq_interval(anchors, indizes)
		
		return np.sqrt(norm_sq)
	
	def scalar_product(self, delay, indizes_1, indizes_2):
		threshold = self.t - delay
		
		i = 0
		while self.past[i+1][0] < threshold:
			i += 1
		
		# partial scalar product of first relevant interval
		anchors = (self.past[i],self.past[i+1])
		sp = scalar_product_partial(anchors, indizes_1, indizes_2, threshold)
		
		# full scalar product of all others
		for i in range(i+1, len(self.past)-1):
			anchors = (self.past[i],self.past[i+1])
			sp += scalar_product_interval(anchors, indizes_1, indizes_2)
		
		return sp
	
	def scale_past(self, indizes, factor):
		for anchor in self.past:
			anchor[1][indizes] *= factor
			anchor[2][indizes] *= factor
	
	def subtract_from_past(self, indizes_1, indizes_2, factor):
		for anchor in self.past:
			anchor[1][indizes_1] -= factor*anchor[1][indizes_2]
			anchor[2][indizes_1] -= factor*anchor[2][indizes_2]
	
	def orthonormalise(self, n_lyap, delay):
		"""
		Orthonormalise separation functions (with Gram-Schmidt) and return their norms after orthogonalisation (but before normalisation).
		"""
		
		vectors = np.split(np.arange(self.n, dtype=int), n_lyap+1)[1:]
		
		norms = []
		for i,vector in enumerate(vectors):
			for j in range(i):
				sp = self.scalar_product(delay, vector, vectors[j])
				self.subtract_from_past(vector, vectors[j], sp)
			norm = self.norm(delay, vector)
			self.scale_past(vector, 1./norm)
			norms.append(norm)
		
		return np.array(norms)
	