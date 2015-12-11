#!/usr/bin/env python3
"""
Log parser for the tree-based routing protocol program logs.

Lecture of Sensor Networks and Internet of Things, Uni Bern
Last modification: 11 December 2015
Author: Sébastien Vaucher
"""
import os
import xml
from xml.sax.handler import ContentHandler
import re

import functools

import math

all_nodes = {}
hop_count_distribution = []


class Node:
    def __init__(self, nid, x=0, y=0, z=0):
        self.nid = nid
        self.x = x
        self.y = y
        self.z = z
        self.parent = None
        self.children = []

    def __repr__(self, *args, **kwargs):
        return "%d, %d, %d, %d" % (self.nid, self.x, self.y, self.z)

    def add_child(self, child):
        self.children.append(child)

    def remove_child(self, child):
        self.children.remove(child)

    @staticmethod
    def parse_urn_id(urn):
        m = re.search("urn:wisebed:node:ubern:([0-9]+)", urn)
        return int(m.group(1))


class XmlHandler(ContentHandler):
    def __init__(self):
        super().__init__()
        self.location = []

    def startElement(self, name, attrs):
        self.location.append(name)

    def endElement(self, name):
        self.location.pop()


class InstantiateNodesHandler(XmlHandler):
    def startElement(self, name, attrs):
        super().startElement(name, attrs)

        if self.location == ['wiseml', 'setup', 'node']:
            nid = Node.parse_urn_id(attrs['id'])
            self.node = Node(nid)
            all_nodes[nid] = self.node

    def characters(self, content):
        if self.location[:4] == ['wiseml', 'setup', 'node', 'position'] and len(self.location) == 5:
            setattr(self.node, self.location[4], int(content))


class BuildTreeHandler(XmlHandler):
    def startElement(self, name, attrs):
        super().startElement(name, attrs)

        if self.location == ['wiseml', 'trace', 'node']:
            nid = Node.parse_urn_id(attrs['id'])
            self.node = all_nodes[nid]

    def characters(self, content):
        if self.location == ['wiseml', 'trace', 'node', 'data']:
            m = re.search("New parent node: ([0-9]+)", content)
            if m is not None:
                if self.node.parent is not None:
                    self.node.parent.remove_child(self.node)

                parent = all_nodes[int(m.group(1))]
                self.node.parent = parent
                parent.add_child(self.node)


class DistributionHandler(XmlHandler):
    def characters(self, content):
        global hop_count_distribution
        if self.location == ['wiseml', 'trace', 'node', 'data']:
            m = re.search('^stats (([0-9]+ ?)+)$', content)
            if m is not None:
                hop_count_distribution = [int(x) for x in m.group(1).split(' ') if x not in ('', '0')]


def print_tree(node, level=0):
    print("|\t" * level, end='')
    print(node.nid)
    for child in node.children:
        print_tree(child, level + 1)


def tikz_relations(node, file_handle, color, phase):
    for child in node.children:
        file_handle.write(
            "\draw [%s, thick, dash pattern= on 4mm off 4mm, dash phase=%dmm, <-, >=latex] (%d,%d,%d) to (%d,%d,%d);\n" % (
                color, phase, node.x, node.y, node.z, child.x, child.y, child.z))
        tikz_relations(child, file_handle, color, phase)


def export_tikz(file_handle):
    global all_nodes
    print("Writing %s" % file_handle.name)
    file_handle.write(r"""
    \begin{tikzpicture}[scale=0.097,y= {(0.281cm,0.281cm)}, z={(0cm,1cm)}, x={(1cm,0cm)}]]
    \node[anchor=south west,inner sep=0] at (0,0) {\includegraphics[width=\textwidth]{3D_image_iam_complete.png}};
    """)
    phase = 2
    for color, file in zip(('Maroon', 'JungleGreen'),
                           ['logs/' + x for x in os.listdir('logs') if re.match('xmac_.+\.wiseml$', x)]):
        print(file)
        all_nodes = {}
        xml.sax.parse(open(file), InstantiateNodesHandler())
        xml.sax.parse(open(file), BuildTreeHandler())
        tikz_relations(all_nodes[1], file_handle, color, phase)
        phase += 4

    for node in all_nodes.values():
        file_handle.write(
            r'\node [blue, very thick] (%d) at (%d,%d,%d) {$\bullet$};' % (node.nid, node.x, node.y, node.z))
    file_handle.write(r'\end{tikzpicture}')


def compute_total_distance(node):
    def distance(n1, n2):
        return math.sqrt((n1.x - n2.x) ** 2 + (n1.y - n2.y) ** 2 + (n1.z - n2.z) ** 2)

    nb_links = 0
    total_distance = 0

    for child in node.children:
        nb_links += 1
        total_distance += distance(node, child)

        child_data = compute_total_distance(child)
        nb_links += child_data[1]
        total_distance += child_data[0]

    return total_distance, nb_links


def output_distribution_stats(file):
    print("Stats for %s:" % file)
    # Average hop-count
    average_hc = sum([x[0] * x[1] for x in zip(hop_count_distribution, range(1, len(hop_count_distribution)))]) / sum(
        hop_count_distribution)
    print("\tAverage hop-count = %f" % average_hc)

    # Global delivery rate
    gdr = sum(hop_count_distribution[1:]) / (len(all_nodes) - 1) / hop_count_distribution[0] * 100.0
    print("\tGlobal delivery rate = %f %%" % gdr)

    if 'xmac' in str(file):
        # Connected delivery rate
        non_connected_nodes = len([node for node in all_nodes.values() if node.parent is None])
        cdr = sum(hop_count_distribution[1:]) / (len(all_nodes) - non_connected_nodes) / hop_count_distribution[
            0] * 100.0
        print("\tConnected delivery rate = %f %%" % cdr)
        cn_percent = (len(all_nodes) - non_connected_nodes) / len(all_nodes) * 100.0
        print("\tConnected nodes = %f %%" % cn_percent)

        # Average distance
        (distance, nb_links) = compute_total_distance(all_nodes[1])
        average_distance = distance / nb_links
        print("\tAverage physical distance = %f" % average_distance)


if __name__ == '__main__':
    with open('report/map.tex', 'w') as tikz:
        export_tikz(tikz)

    for file in ['logs/' + x for x in os.listdir('logs') if re.match('.+\.wiseml$', x)]:
        all_nodes = {}
        hop_count_distribution = []
        xml.sax.parse(open(file), InstantiateNodesHandler())
        xml.sax.parse(open(file), BuildTreeHandler())
        xml.sax.parse(open(file), DistributionHandler())
        output_distribution_stats(file)
