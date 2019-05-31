import parser
import matplotlib.pyplot as plt
from matplotlib.colors import ListedColormap
import numpy as np

def get_weight_cmap():
	cmap = plt.cm.RdBu
	my_cmap = cmap(np.arange(cmap.N))
	my_cmap[:,-1] = 0.5*abs(np.linspace(-1, 1, cmap.N))
	my_cmap = ListedColormap(my_cmap)
	return my_cmap

def plot_analytic_2nif_harmonic(min_lim, max_lim, LEVELS, alpha=1.0):
	def psi0(x):
                return np.exp(-x**2/2)

        def psi1(x):
                return x * psi0(x)

        xs = np.linspace(min_lim,max_lim,100)
        xs, ys = np.meshgrid(xs, xs)
        zs = [psi0(x)*psi1(y)-psi0(y)*psi1(x) for x, y in zip(xs,ys)]

        plt.contour(xs,ys,zs,LEVELS,alpha=alpha)

def plot_2nif_fast(wavefunction):
	
	wfn = parser.transpose_wavefunction(wavefunction)
	xs = [x[0] for x in wfn[1]]
	ys = [x[0] for x in wfn[2]]
	zs = wfn[0]

	plt.scatter(xs,ys,c=zs,cmap=get_weight_cmap())
	plot_analytic_2nif_harmonic(-4,4,40,alpha=0.5)
	plt.xlim([-4,4])
	plt.ylim([-4,4])

def plot_2nif(start_iter, end_iter):
	wavefunction = parser.parse_wavefunction(start_iter, end_iter)
	iterations = len(wavefunction)
	wfn = parser.transpose_wavefunction(wavefunction)

	particles = len(wfn)-1
	coords    = len(wfn[1][0])

	if particles != 2:
		print "Error ", sys.argv[0], " only works with 2 particle systems!"
		quit()

	if coords != 1:
		print "Error ", sys.argv[0], " only works with 1 dimensional systems!"
		quit()

	xs = [x[0] for x in wfn[1]]
	ys = [x[0] for x in wfn[2]]
	zs = wfn[0]

	fs = "{0} walkers from {1} dmc iteration(s) {2} to {3}"
	plt.suptitle(fs.format(len(zs), iterations, start_iter, end_iter))

	RES    = 20
	LEVELS = 40
	min_lim = -4
	max_lim = 4

	plt.subplot(2,2,3)
	alpha = min(0.2, 200.0/len(zs))

	plt.scatter(xs, ys, c=zs, cmap=get_weight_cmap())
	plt.xlim([min_lim,max_lim])
	plt.ylim([min_lim,max_lim])

	xn  = np.linspace(min_lim, max_lim, RES)
	yn  = np.linspace(min_lim, max_lim, RES)

	for iplt, tau in enumerate([0.05, 0.5]):

		zn  = [] 
		for y in yn:
			row = []
			for x in xn:
				val = 0
				for xi, yi, zi in zip(xs, ys, zs):
					dx = xi - x
					dy = yi - y
					val += zi*np.exp(-(dx**2+dy**2)/(2*tau))
				row.append(val)
			zn.append(row)

		xplot, yplot = np.meshgrid(xn, yn)

		plt.subplot(2,2,1+iplt)
		plt.contour(xplot, yplot, zn, LEVELS)
		plt.plot([min_lim, max_lim], [min_lim, max_lim], color="black")

	plt.subplot(224)
	plot_analytic_2nif_harmonic(min_lim, max_lim, LEVELS)
