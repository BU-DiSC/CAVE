import networkit as nk
import numpy as np
import os
import sys
import struct

N = 10000

def write_binary(G, data_name):
    # Binary edge list
    path = os.path.join('..', 'data', "%s.binedge" % data_name)
    with open(path, "wb") as out_file:
        for (u, v) in G.iterEdges():
            out_file.write(struct.pack("<l", u))
            out_file.write(struct.pack("<l", v))

            out_file.write(struct.pack("<l", v))
            out_file.write(struct.pack("<l", u))

    # Binary adjacent list
    path = os.path.join('..', 'data', "%s.binadj" % data_name)
    with open(path, "wb") as out_file:

        out_file.write(struct.pack("<l", G.numberOfNodes()))
        out_file.write(struct.pack("<l", G.numberOfEdges()))
        for u in G.iterNodes():
            out_file.write(struct.pack("<l", G.degree(u)))
            for v in G.iterNeighbors(u):
                out_file.write(struct.pack("<l", v))


def write_texts(G, data_name):
    # Write adjacent list
    # path = os.path.join('..', 'data', f"{data_name}.adjlist")
    # nk.graphio.writeGraph(G, path, nk.graphio.Format.METIS)
    # Write edge list
    path = os.path.join('..', 'data', f"{data_name}.edgelist")
    nk.graphio.writeGraph(G, path, nk.graphio.Format.EdgeList, 
                          separator=' ', firstNode=0, bothDirections=False)


def gen_BA(N):
    m = 25
    gen = nk.generators.BarabasiAlbertGenerator(m, N)
    G = gen.generate()
    G = nk.graphtools.toUndirected(G)
    print(G.numberOfNodes(), G.numberOfEdges())

    write_texts(G, f"BA_{N}_{m}")
    # write_binary(G, f"BA_{N}_{m}")


def gen_ER(N):
    gen = nk.generators.ErdosRenyiGenerator(N, np.log(N)/N)
    G = gen.generate()
    G = nk.graphtools.toUndirected(G)
    print(G.numberOfNodes(), G.numberOfEdges())
    # write_texts(G, f"ER_{N}")
    write_binary(G, f"ER_{N}")


if __name__ == "__main__":

    nk.setSeed(42, True)

    if len(sys.argv) > 1:
        N = int(sys.argv[1])

    gen_BA(N)
    # gen_ER(N)
