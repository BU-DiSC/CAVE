import networkx as nx
import pandas as pd
import numpy as np
import sys
import os


def write_adjlist(file_name, graph):
    '''
    Write graph to file in METIS format
    file_name:
    graph:
    '''
    graph_dict = nx.to_dict_of_lists(G=graph)
    path = os.path.join('..', 'data', '%s.adjlist' % file_name)

    with open(path, 'w') as f:
        print(nx.number_of_nodes(graph), end=' ', file=f)
        print(nx.number_of_edges(graph), file=f)
        for item in graph_dict.items():
            # print(item[0], end=' ', file=f)
            for node in item[1]:
                print(node, end=' ', file=f)
            print(file=f)


if __name__ == '__main__':
    if (len(sys.argv) < 3):
        print('[Usage]: python ./graph_parser.py [format] [file_path]')
        exit(1)

    data_format = sys.argv[1]
    file_path = sys.argv[2]

    if data_format == "edgelist":
        G = nx.read_edgelist(file_path)
    elif data_format == "adjlist":
        G = nx.read_adjlist(file_path)
    else:
        exit(1)

    # G = G.to_undirected()
    G = nx.convert_node_labels_to_integers(G, 1)

    print("Connectivity:", nx.is_connected(G))
    print("CCs:", nx.number_connected_components(G))

    base_name = os.path.basename(file_path).rsplit('.', 1)[0]

    write_adjlist(base_name, G)
