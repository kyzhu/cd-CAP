#include <cstdio>
#include <cstring>
#include <ctime>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>
#include <math.h>
#include <queue>
#include <algorithm>
#include <vector>
#include <utility>
#include <ilcplex/ilocplex.h>
using namespace std;

typedef unsigned long long llu;

typedef std::pair<int, int> colored_node;
typedef std::set<colored_node> colored_node_set;

struct colored_node_set_hash {
	size_t operator()(const colored_node_set& cns) const {
		size_t hashvalue = 0;
		for (auto node : cns)
			hashvalue += node.first;
		return hashvalue;
	}
};

struct PatientBitmask {
	int maxSize = 0;
	int size = 0;
	int len = 0;
	llu * bits = NULL;

	PatientBitmask() {
	}

	PatientBitmask(int maxSize) : maxSize(maxSize) {
		size = 0;
		len = ceil(maxSize / 64.0);
		bits = new llu [len];
		memset(bits, 0, sizeof(llu) * len);
	}

	PatientBitmask(PatientBitmask *Q) {
		maxSize = Q->maxSize;
		size = Q->size;
		len = Q->len;
		bits = new llu [len];
		for (int i = 0; i < len; i++) 
			bits[i] = Q->bits[i];
	}

	~PatientBitmask () {
		delete []bits;
	}

	void setBit(int pos, bool val) {
		int idx = pos / 64;
		if (idx >= len) {
			fprintf(stderr, "< Error > Cannot assign bit because bitmask is too small. pos: %d | len: %d | idx: %d | maxSize: %d\n", pos, len, idx, maxSize);
			exit(0);
		}
		int bitIdx = pos % 64;
		bool oldVal = getBit(pos);
		if (oldVal ^ val) {	// the bit is about to get changed
			bits[idx] ^= llu(1) << bitIdx;
			if (val) size++;
			else size--;
		}
	}

	bool getBit(int pos) const {
		int idx = pos / 64;
		if (idx >= len) {
			fprintf(stderr, "< Error > Cannot return bit because bitmask is too small.\n");
			exit(0);
		}
		int bitIdx = pos % 64;
		return bits[idx] & (llu(1) << bitIdx);
	}

	int getSize() const { return size; }

	int getPositionOfFirstSetBit() const {
		if (size == 0) {
			fprintf(stderr, "< Error > Cannot return position of first set bit because bitmask is empty.\n");
			exit(0);
		}
		for (int i = 0; i < len; i++) {
			if (bits[i]) return i*64 + __builtin_ctzll(bits[i]);
		}
	}

	void mergeBitmask(PatientBitmask *Q) {
		size = 0;
		for (int i = 0; i < len; i++) {
			bits[i] = bits[i] & Q->bits[i];
			size += __builtin_popcountll(bits[i]);
		}
	}
};

/* Fancy Output Functions. */
void printHeader(const char * text) {
	int n = strlen(text);
	fprintf( stderr, "\n" );
	for (int i = 0; i < n + 4; i++) fprintf(stderr, "*");
	fprintf(stderr, "\n* %s *\n", text);
	for (int i = 0; i < n + 4; i++) fprintf(stderr, "*");
	fprintf(stderr, "\n");
}

/* Global variables. */
struct stringPairHash {
	size_t operator() (const pair<string, string> & Q) const {
		size_t h1 = hash<string>() (Q.first);
		size_t h2 = hash<string>() (Q.second);
		return h1 ^ (h2 << 1);
	}
};

struct Entry {
	string * names;
	unordered_map<string, int> indices;
} samples, genes, alterations;

struct Graph {
	int V, E;
	int * NSize;
	int ** N;
	string * nodeNames;
	unordered_map<int, int> * idxInNeighbourList;	// idxInNeighbourList[a][b]=c means that "In node a's neighbor list, node b is at position c."
	unordered_map<int, int> * incomingEdges;	// incomingEdges[a][b]=c means that "There is an incoming edge into a from b's edge list at position c"
	unordered_map<string, int> nodeIndices;
	int numCC;		// number of connected components.
	int * ccIndex;	// connected component index for each node.
	int * ccSize;	// size of each connected component
} G;

unordered_map<int, unsigned int> * geneAlterations;	// geneAlterations[ i ][ j ] = c means that "gene i has color c in patient j". The colors are bitmasks (so supporting max 32 different alterations).

void findConnectedComponents() {
	G.numCC = 0;
	G.ccIndex = new int[ G.V ];
	memset(G.ccIndex, -1, sizeof(G.ccIndex[0]) * G.V);
	int * nodeStack = new int[G.V];
	int nodeStackSize = 0;
	for (int i = 0; i < G.V; i++) {
		if (G.ccIndex[i] == -1) {
			G.ccIndex[i] = G.numCC++;
			nodeStack[nodeStackSize++] = i;
			while (nodeStackSize) {
				int node = nodeStack[--nodeStackSize];
				for (int j = 0; j < G.NSize[node]; j++) {
					int const & neighbor = G.N[node][j];
					if (G.ccIndex[neighbor] == -1) {
						G.ccIndex[neighbor] = G.ccIndex[node];
						nodeStack[nodeStackSize++] = neighbor;
					}
				}
			}
		}
	}
	G.ccSize = new int[G.numCC];
	memset(G.ccSize, 0, sizeof(G.ccSize[0]) * G.numCC);
	for (int i = 0; i < G.V; i++) G.ccSize[ G.ccIndex[i] ]++;
	delete [] nodeStack;
	fprintf(stderr, "\tInput network contains %d connected components.\n", G.numCC);
	//for (int i = 0; i < G.numCC; i++)
	//	cout << G.ccSize[i] << endl; 
}

/*
	Reads the input -n parameter as a collection of undirected edges (pairs of node names, separated by whitespace) and stores the information in global Graph object G.
*/
void readUndirectedNetwork(const char * filename) {
	fprintf(stderr, "Reading the network... ");
	int timerStart = clock();
	unordered_set<pair<string, string>, stringPairHash> uniqueEdges;
	char u[1000], v[1000];
	FILE * fin = fopen(filename, "r");
	G.V = 0;
	while (fscanf(fin, "%s%s", u, v) == 2) {
		char * a = u, * b = v;
		if (strcmp(a, b) > 0) swap(a, b);
		else if (strcmp(a, b) == 0) continue;
		pair<string, string> e = make_pair(string(a), string(b));
		if (uniqueEdges.find(e) == uniqueEdges.end()) {	// Previously NOT seen undirected edge
			uniqueEdges.insert(e);
			if (G.nodeIndices.find(e.first) == G.nodeIndices.end()) {
				G.nodeIndices[e.first] = G.V;
				G.V++;
			}
			if (G.nodeIndices.find(e.second) == G.nodeIndices.end()) {
				G.nodeIndices[e.second] = G.V;
				G.V++;
			}
		}
	}
	fclose(fin);
	G.E = uniqueEdges.size() * 2;

	G.NSize = new int [G.V];
	G.N = new int * [G.V];
	G.nodeNames = new string[G.V];
	memset(G.NSize, 0, sizeof(G.NSize[0]) * G.V);

	for (auto e: uniqueEdges) {
		int idx1 = G.nodeIndices[e.first];
		if (G.nodeNames[idx1].size() == 0) G.nodeNames[idx1] = e.first;

		int idx2 = G.nodeIndices[e.second];
		if (G.nodeNames[idx2].size() == 0) G.nodeNames[idx2] = e.second;

		G.NSize[idx1]++;
		G.NSize[idx2]++;
	}

	for (int i = 0; i < G.V; i++) {
		G.N[i] = new int[ G.NSize[i] ];
	}

	int * tempNSize = new int [G.V];
	memset(tempNSize, 0, sizeof(tempNSize[0]) * G.V );
	G.idxInNeighbourList = new unordered_map<int, int>[G.V];
	G.incomingEdges = new unordered_map<int, int>[G.V];

	for (auto e: uniqueEdges) {
		int idx1 = G.nodeIndices[e.first];
		int idx2 = G.nodeIndices[e.second];
		G.N[ idx1 ][ tempNSize[idx1] ] = idx2;
		G.idxInNeighbourList[idx1][idx2] = tempNSize[idx1];
		G.incomingEdges[idx2][idx1] = tempNSize[idx1];
		G.N[ idx2 ][ tempNSize[idx2] ] = idx1;
		G.idxInNeighbourList[idx2][idx1] = tempNSize[idx2];
		G.incomingEdges[idx1][idx2] = tempNSize[idx2];
		tempNSize[idx1]++;
		tempNSize[idx2]++;
	}
	delete [] tempNSize;
	fprintf(stderr, "done. (%.2lf seconds)\n", double(clock() - timerStart) / CLOCKS_PER_SEC);
	fprintf(stderr, "\tInput network contains %d nodes and %d undirected edges.\n", G.V, G.E / 2);
}

/*
	Reads the input -l parameter as a collection of "sample gene alterationType" triples, separated by whitespace.
*/
void readAlterationProfiles(const char * filename) {
	fprintf(stderr, "Reading the alteration profiles... ");
	int timerStart = clock();
	char sample[1000], gene[1000], alterationType[1000];
	FILE * fin = NULL;
	if (!(fin = fopen(filename, "r"))) {
		fprintf(stderr, "\n< Error > Cannot open file '%s'. Please make sure the file exists.\n", filename);
		exit(0);
	}
	while (fscanf(fin, "%s%s%s", sample, gene, alterationType) == 3) {
		if ( G.nodeIndices.find( string(gene) ) == G.nodeIndices.end() ) continue;
		if ( samples.indices.find( string(sample) ) == samples.indices.end() ) {
			int idx = samples.indices.size();
			samples.indices[ string(sample) ] = idx;
		}
		if ( genes.indices.find( string(gene) ) == genes.indices.end() ) {
			int idx = genes.indices.size();
			genes.indices[ string(gene) ] = idx;
		}
		if ( alterations.indices.find( string(alterationType) ) == alterations.indices.end() ) {
			int idx = alterations.indices.size();
			alterations.indices[ string(alterationType) ] = idx;
		}
	}
	samples.names = new string[ samples.indices.size() ];
	genes.names = new string[ genes.indices.size() ];
	alterations.names = new string[ alterations.indices.size() ];
	for (auto it : samples.indices) {
		samples.names[it.second] = it.first;
	}
	for (auto it : genes.indices) {
		genes.names[it.second] = it.first;
	}
	for (auto it : alterations.indices) {
		alterations.names[it.second] = it.first;
	}
	rewind(fin);
	geneAlterations = new unordered_map<int, unsigned int> [ G.V ];
	while (fscanf(fin, "%s%s%s", sample, gene, alterationType) == 3) {
		if ( G.nodeIndices.find( string(gene) ) == G.nodeIndices.end() ) continue;
		int geneIndex = G.nodeIndices[string(gene)];
		int sampleIndex = samples.indices[sample];
		// int subnetworkIndex = G.nodeIndices[string(gene)] + sampleIndex * G.V;
		//subnetworkSeeds.push_back(make_pair(sampleIndex, G.nodeIndices[string(gene)]));
		int alterationIndex = alterations.indices[alterationType];
		if ( geneAlterations[geneIndex].find(sampleIndex) == geneAlterations[geneIndex].end() ) geneAlterations[geneIndex][sampleIndex] = 0;
		int bitMask = (1 << alterationIndex);
		geneAlterations[geneIndex][sampleIndex] |= bitMask;
	}
	fclose(fin);
	fprintf(stderr, "done. (%.2lf seconds)\n", double(clock() - timerStart) / CLOCKS_PER_SEC);
	fprintf( stderr, "\tThere are %lu samples, with a total of %lu genes, harboring %lu different alterations.\n", samples.indices.size(), genes.indices.size(), alterations.indices.size() );
	//fprintf(stderr, "\tThere are %lu possible subnetwork seeds.\n", subnetworkSeeds.size());
}

void runILPSolverMaxNetworkSize(int minPatientSupport, int num_threads) {
	IloEnv env;
	IloModel model(env);
	IloExpr objective(env);
	IloBoolVarArray X(env, G.V);
	for (int j = 0; j < G.V; j++) {
		char name[ 100 ];
		sprintf(name, "X%d", j);
		X[j] = IloBoolVar(env, name);
	}

	IloBoolVarArray P(env, samples.indices.size());
	for (size_t i = 0; i < samples.indices.size(); i++) {
		char name[ 100 ];
		sprintf(name, "P%lu", i);
		P[i] = IloBoolVar(env, name);
	}

	IloBoolVarArray S(env, G.V);
	for (int j = 0; j < G.V; j++) {
		char name[ 100 ];
		sprintf(name, "S%d", j);
		S[j] = IloBoolVar(env, name);
	}

	IloNumVarArray sourceEdges(env, G.V);
	for (int j = 0; j < G.V; j++)  {
		char name[ 100 ];
		sprintf(name, "Se%d", j);
		sourceEdges[j] = IloNumVar(env, 0, G.V, name);
	}

	IloNumVarArray * capacities = new IloNumVarArray [G.V];
	for (int j = 0; j < G.V; j++) {
		capacities[j] = IloNumVarArray(env, G.NSize[j]);
		for (int nIdx = 0; nIdx < G.NSize[j]; nIdx++) {
			char name[ 100 ];
			sprintf(name, "F%d,%d", j, nIdx);
			capacities[j][nIdx] = IloNumVar(env, 0, G.V, name);
		}
	}
	fprintf(stderr, "\tConstructed the variables.\n");

	/* Objective: Maximize the size of subgraph. */
	for (int j = 0; j < G.V; j++)
		objective += X[j];
	model.add( IloMaximize(env, objective) );
	fprintf(stderr, "\tConstructed the objective function.\n");

	/* Constraint: There must be at least t patients that have the same color on each X[j] = 1. */
	{
		IloExpr e(env);
		for (size_t i = 0; i < samples.indices.size(); i++)
			e += P[i];
		model.add( e >= minPatientSupport );
	}
	fprintf(stderr, "\tConstructed constraint (3.1).\n");
	
	/* Constraints: P[i], P[i'] and X[j] should be consistent*/
	std::vector<std::vector<int>> color_count(G.V, std::vector<int>(alterations.indices.size()));
	for (int j = 0; j < G.V; j++)
		for (size_t i = 0; i < samples.indices.size(); i++)
			for (size_t a = 0; a < alterations.indices.size(); a++)
				if ((geneAlterations[j][i] && (1 << a)) != 0)
					color_count[j][a]++;
	
	for (int j = 0; j < G.V; j++) {
		int max = color_count[j][0];
		for (size_t i = 1; i < alterations.indices.size(); i++)
			if (color_count[j][i] > max)
				max = color_count[j][i];
		if (max < minPatientSupport)
			model.add(X[j] == 0);
		else
			for (size_t i = 0; i < samples.indices.size(); i++) {
				bool iHasColor = geneAlterations[j].count(i);
				for (size_t i1 = i + 1; i1 < samples.indices.size(); i1++) {
					bool i1HasColor = geneAlterations[j].count(i1);
					if ( iHasColor && i1HasColor && (geneAlterations[j][i] & geneAlterations[j][i1]) ) 
						/* model.add( P[i] + P[i1] + X[j] <= 3 ) */ ;
					else	
						model.add( P[i] + P[i1] + X[j] <= 2 );
				}
			}
	}
	fprintf(stderr, "\tConstructed constraint (3.5).\n");

	/* Constraint: Only one node can be the seed of the optimal subnetwork. */
	{
		IloExpr e(env);
		for (int j = 0; j < G.V; j++)
			e += S[j];
		model.add( e == 1 );
	}
	fprintf(stderr, "\tConstructed constraint (3.6).\n");

	/* Constraint: The seed is trivially chosen to be part of the subnetwork. */
	for (int j = 0; j < G.V; j++)
		model.add( X[j] >= S[j] );
	fprintf(stderr, "\tConstructed constraint (3.7).\n");

	/* Constraint: We let flow from the source go only through the node marked as the seed. */
	for (int j = 0; j < G.V; j++)
		model.add( sourceEdges[j] - G.V * S[j] <= 0 );
	fprintf(stderr, "\tConstructed constraint (3.8).\n");

	/* Constraint: The constraint of total amount of flow. */
	{
		IloExpr e(env);
		for (int j = 0; j < G.V; j++) {
			e += sourceEdges[j];
			e -= X[j];
		}
		model.add( e == 0 );
	}
	fprintf(stderr, "\tConstructed constraint (3.9).\n");

	/* Constraints: If there is flow entering a node, then it must be part of the subnetwork. */
	for (int j = 0; j < G.V; j++) {
		IloExpr e(env);
		e += sourceEdges[j];
		for ( auto edge : G.incomingEdges[j] ) {
			int sourceIdx = edge.first;
			int edgeOffset = edge.second;
			e += capacities[sourceIdx][edgeOffset];
		}
		model.add( e <= G.V * X[j] );
	}
	fprintf(stderr, "\tConstructed constraint (3.10).\n");

	/* Constraints: Flow must be conserved. */
	for (int j = 0; j < G.V; j++) {
		IloExpr e(env);
		e += sourceEdges[j];
		for ( auto edge : G.incomingEdges[j] ) {
			int sourceIdx = edge.first;
			int edgeOffset = edge.second;
			e += capacities[sourceIdx][edgeOffset];
		}
		for (int nIdx = 0; nIdx < G.NSize[j]; nIdx++)
			e -= capacities[j][nIdx];
		e -= X[j];
		model.add(e == 0);
	}
	fprintf(stderr, "\tConstructed constraint (3.11).\n");

	try {
		IloCplex cplex(model);
		cplex.exportModel("model.lp");
		fprintf(stderr, "ILP model file written to 'model.lp'.\n");
		if (num_threads < 32)
			cplex.setParam(IloCplex::IntParam::Threads, num_threads);
		cplex.setParam(IloCplex::NumParam::TiLim, 3600); // 10 hours
		//cplex.setParam(IloCplex::MIPEmphasis, 1);
		IloBool feasible = cplex.solve();
		cplex.writeSolution("solution.txt");
		fprintf(stderr, "ILP solution file written to 'solution.txt'.\n");
		FILE * fout = fopen("output.txt", "w");
		if (feasible)
			fprintf(fout, "Solution is feasible.\n");
		for (int j = 0; j < G.V; j++) {
			if (cplex.getValue( S[j] ) == 1 ) {
				fprintf(fout, "Seed: %d, %s\n", j, G.nodeNames[j].c_str());
				break;
			}
		}
		fprintf(fout, "Genes:");
		for (int j = 0; j < G.V; j++)
			if (cplex.getValue( X[j] ) == 1 ) fprintf(fout, "\n%d,%s", j, G.nodeNames[j].c_str());
		fprintf(fout, "\nPatients:");
		for (size_t i = 0; i < samples.indices.size(); i++)
			if (cplex.getValue( P[i] ) == 1 ) fprintf(fout, "\n%s", samples.names[i].c_str());
		fprintf(fout, "\nFlow Values:"); 
		for (int i = 0; i < G.V; i++)
			for (int j = 0; j < G.NSize[i]; j++)
				if (cplex.getValue(capacities[i][j]) > 0) {
					int sinkIdx = -1;
					for (auto index : G.idxInNeighbourList[i]) {
						int sourceIdx = index.first;
						int edgeOffset = index.second;
						if (edgeOffset == j) {
							sinkIdx = sourceIdx;
							break;
						}
					}
					fprintf(fout, "c[%d][%d] = %f\n", i, sinkIdx, cplex.getValue(capacities[i][j]));
				}
		fclose(fout);
	}
	catch (IloException &ex) {
		fprintf(stderr, "\tError occurs in ILP construction and solving.\n");
		fprintf(stderr, "%s\n", ex.getMessage());
	}
}

void solveMaxNetworkSize(int minPatientSupport) {
	std::vector<std::unordered_map<colored_node_set, int, colored_node_set_hash>> subNetwork;
	std::vector<std::unordered_map<int, PatientBitmask*>> patientProfile;
	std::unordered_map<colored_node_set, int, colored_node_set_hash> subNetwork_0;
	subNetwork.push_back(subNetwork_0);
	std::unordered_map<int, PatientBitmask*> patientProfile_0;
	patientProfile.push_back(patientProfile_0);

	int color_count[G.V][alterations.indices.size() + 1] = {0};
	for (int j = 0; j < G.V; j++)
		for (size_t i = 0; i < samples.indices.size(); i++)
			for (size_t a = 0; a < alterations.indices.size(); a++)
				if ((geneAlterations[j][i] & (1 << a)) != 0)
					color_count[j][a + 1]++;
	//for (int j = 0; j < G.V; j++)
	//	cout << color_count[j][1] << " " << color_count[j][2] << " " << color_count[j][3] << " " << color_count[j][4] << endl;
	
	int sum = 0;
	for (int j = 0; j < G.V; j++) {
		for (size_t i = 1; i <= alterations.indices.size(); i++)
			if (color_count[j][i] >= minPatientSupport) {
				color_count[j][i] = 1;
				color_count[j][0] = 1;
			}
			else
				color_count[j][i] = 0;
	}
	for (int j = 0; j < G.V; j++)
		if (color_count[j][0] == 1)
			sum++;
	fprintf(stderr, "There are %d nodes where at least %d patients are mutated.\n", sum, minPatientSupport);

	int valid = 0;
	for (int j = 0; j < G.V; j++)
		if (color_count[j][0] == 1)
			for (auto index : G.idxInNeighbourList[j])
				if (color_count[index.first][0] == 1 && index.first > j)
					for (size_t k = 1; k <= alterations.indices.size(); k++)
						for (size_t l = 1; l <= alterations.indices.size(); l++)
							if (color_count[j][k] == 1 && color_count[index.first][l] == 1) {
								PatientBitmask *profile_e_k_l = new PatientBitmask(samples.indices.size());
								colored_node_set edge;
								colored_node node_1 (j, (int)k);
								edge.insert(node_1);
								colored_node node_2 (index.first, (int)l);
								edge.insert(node_2);
								sum = 0;
								for (size_t m = 0; m < samples.indices.size(); m++)
									if ((geneAlterations[j][m] & (1 << (k - 1))) && (geneAlterations[index.first][m] & (1 << (l - 1)))) {
										sum++;
										profile_e_k_l->setBit(m, 1);
									}
								if (sum >= minPatientSupport) {
									auto result = subNetwork[0].insert(std::pair<colored_node_set, int> (edge, valid));
									if (result.second) {
										patientProfile[0][valid] = profile_e_k_l;
										valid++;
									}
								}
							}	
	fprintf(stderr, "There are %lu edges where at least %d patients are mutated at each node.\n", subNetwork[0].size(), minPatientSupport); 

	/* Run Apriori Algorithm. */
	size_t network_size = 1;
	while (true) {
		std::unordered_map<colored_node_set, int, colored_node_set_hash> nextsubNetwork;
		std::unordered_map<int, PatientBitmask*> nextpatientProfile;

		valid = 0;
		
		for (auto s1 : subNetwork[network_size - 1]) {
			for (auto s2 : subNetwork[0]) {
				colored_node_set candidate = s1.first;
				candidate.insert(s2.first.begin(), s2.first.end());
				if (candidate.size() == network_size + 2) {
					PatientBitmask *profile_candidate = new PatientBitmask(patientProfile[network_size - 1][s1.second]);
					profile_candidate->mergeBitmask(patientProfile[0][s2.second]);
					if (profile_candidate->getSize() >= minPatientSupport) {
						auto result = nextsubNetwork.insert(std::pair<colored_node_set, int>(candidate, valid));
						if (result.second) {
							nextpatientProfile[valid] = profile_candidate;
							valid++;
						}
					}
				}
			}
		}

		/* Found the largest subnetwork. */
		if (nextsubNetwork.empty()) {
			fprintf(stderr, "The maximum subnetwork size is %lu.\n", network_size + 1);
			break;
		}
		/* Try next level. */
		fprintf(stderr, "There are %lu subnetworks of size %lu, where at least %d patients are mutated at each node.\n", nextsubNetwork.size(), network_size + 2, minPatientSupport);
		subNetwork.push_back(nextsubNetwork);
		patientProfile.push_back(nextpatientProfile);
		network_size++;
	}

	/* Output Solution(s). */
	FILE * fout = fopen("output.tsv", "w");
	size_t i = 0;
	fprintf(fout, "Solution\t");
	fprintf(fout, "Nodes\t");
	fprintf(fout, "Color\t");
	fprintf(fout, "SampleID\n");
	for (auto subgraph : subNetwork[subNetwork.size() - 1]) {
		fprintf(fout, "Solution_%lu\t", i + 1);
		size_t k = 0;
		for (auto node : subgraph.first) {
			if (k < subgraph.first.size() - 1) 
				fprintf(fout, "%s:", G.nodeNames[node.first].c_str());
			else
				fprintf(fout, "%s\t", G.nodeNames[node.first].c_str());
			k++;
		}
		
		k = 0;
		for (auto node : subgraph.first) {
			if (k < subgraph.first.size() - 1)
				fprintf(fout, "%s:", alterations.names[node.second - 1].c_str());
			else
				fprintf(fout, "%s\t", alterations.names[node.second - 1].c_str());
			k++;
		}

		k = 0;
		for (size_t j = 0; j < samples.indices.size(); j++)
			if (patientProfile[subNetwork.size() - 1][subgraph.second]->getBit(j) == 1) {
				if (k < patientProfile[subNetwork.size() - 1][subgraph.second]->getSize() - 1)
					fprintf(fout, "%s:", samples.names[j].c_str());
				else
					fprintf(fout, "%s\n", samples.names[j].c_str());
				k++;
			}
		i++;
	}
	fclose(fout);
}

/* Find the maximum subnetwork with depth at least MINPATIENTSUPPORT,
   but allowing DELTA mismatches on each patient. */
void solveMaxNetworkAlmost(int minPatientSupport, double alpha, int delta) {
	/* Temporarily allow DELTA = 1 or DELTA = 2. */
	if (delta <= 0 || delta > 2) {
		fprintf(stderr, "Illegal maximum mismatch bound.");
		abort();
	}
	std::vector<std::unordered_map<colored_node_set, int, colored_node_set_hash>> subNetwork;
	std::vector<std::unordered_map<int, PatientBitmask*>> patientProfile;
	
	std::unordered_map<colored_node_set, int, colored_node_set_hash> subNetwork_0;
	
	std::unordered_map<int, PatientBitmask*> patientProfile_0;
	patientProfile.push_back(patientProfile_0);

	int color_count[G.V][alterations.indices.size() + 1] = {0};
	for (int j = 0; j < G.V; j++)
		for (size_t i = 0; i < samples.indices.size(); i++)
			for (size_t a = 0; a < alterations.indices.size(); a++)
				if ((geneAlterations[j][i] & (1 << a)) != 0)
					color_count[j][a + 1]++;
	int sum = 0;
	for (int j = 0; j < G.V; j++) {
		for (size_t i = 1; i <= alterations.indices.size(); i++)
			if (color_count[j][i] >= minPatientSupport * alpha) {
				color_count[j][i] = 1;
				color_count[j][0] = 1;
			}
			else
				color_count[j][i] = 0;
	}
	for (int j = 0; j < G.V; j++)
		if (color_count[j][0] == 1)
			sum++;
	fprintf(stderr, "There are %d nodes where at least %d patients are mutated.\n", sum, ceil(minPatientSupport * alpha));

	/* First step: grow the networks until size DELTA + 1. */
	int valid = 0;
	size_t network_size = 1;
	for (int j = 0; j < G.V; j++)
		if (color_count[j][0] == 1)
			for (auto index : G.idxInNeighbourList[j])
				if (color_count[index.first][0] == 1 && index.first > j)
					for (size_t k = 1; k <= alterations.indices.size(); k++)
						for (size_t l = 1; l <= alterations.indices.size(); l++)
							if (color_count[j][k] == 1 && color_count[index.first][l] == 1) {
								PatientBitmask *profile_exact_k_l = new PatientBitmask(samples.indices.size());
								PatientBitmask *profile_mismatch_k_l = new PatientBitmask(samples.indices.size());
								colored_node_set edge;
								colored_node node_1 (j, (int)k);
								edge.insert(node_1);
								colored_node node_2 (index.first, (int)l);
								edge.insert(node_2);
								sum = 0;
								for (size_t m = 0; m < samples.indices.size(); m++)
									if ((geneAlterations[j][m] & (1 << (k - 1))) && (geneAlterations[index.first][m] & (1 << (l - 1)))) {
										sum++;
										profile_e_k_l->setBit(m, 1);
									}
								if (sum >= minPatientSupport) {
									auto result = subNetwork[0].insert(std::pair<colored_node_set, int> (edge, valid));
									if (result.second) {
										patientProfile[0][valid] = profile_e_k_l;
										valid++;
									}
								}
							}	
	fprintf(stderr, "There are %lu edges where at least %d patients are mutated at each node.\n", subNetwork[0].size(), minPatientSupport); 

	/* Run Apriori Algorithm. */
	network_size = 1;
	while (true) {
		std::unordered_map<colored_node_set, int, colored_node_set_hash> nextsubNetwork;
		std::unordered_map<int, PatientBitmask*> nextpatientProfile;

		valid = 0;
		
		for (auto s1 : subNetwork[network_size - 1]) {
			for (auto s2 : subNetwork[0]) {
				colored_node_set candidate = s1.first;
				candidate.insert(s2.first.begin(), s2.first.end());
				if (candidate.size() == network_size + 2) {
					PatientBitmask *profile_candidate = new PatientBitmask(patientProfile[network_size - 1][s1.second]);
					profile_candidate->mergeBitmask(patientProfile[0][s2.second]);
					if (profile_candidate->getSize() >= minPatientSupport) {
						auto result = nextsubNetwork.insert(std::pair<colored_node_set, int>(candidate, valid));
						if (result.second) {
							nextpatientProfile[valid] = profile_candidate;
							valid++;
						}
					}
				}
			}
		}

		/* Found the largest subnetwork. */
		if (nextsubNetwork.empty()) {
			fprintf(stderr, "The maximum subnetwork size is %lu.\n", network_size + 1);
			break;
		}
		/* Try next level. */
		fprintf(stderr, "There are %lu subnetworks of size %lu, where at least %d patients are mutated at each node.\n", nextsubNetwork.size(), network_size + 2, minPatientSupport);
		subNetwork.push_back(nextsubNetwork);
		patientProfile.push_back(nextpatientProfile);
		network_size++;
	}

	/* Output Solution(s). */
	FILE * fout = fopen("output.tsv", "w");
	size_t i = 0;
	fprintf(fout, "Solution\t");
	fprintf(fout, "Nodes\t");
	fprintf(fout, "Color\t");
	fprintf(fout, "SampleID\n");
	for (auto subgraph : subNetwork[subNetwork.size() - 1]) {
		fprintf(fout, "Solution_%lu\t", i + 1);
		size_t k = 0;
		for (auto node : subgraph.first) {
			if (k < subgraph.first.size() - 1) 
				fprintf(fout, "%s:", G.nodeNames[node.first].c_str());
			else
				fprintf(fout, "%s\t", G.nodeNames[node.first].c_str());
			k++;
		}
		
		k = 0;
		for (auto node : subgraph.first) {
			if (k < subgraph.first.size() - 1)
				fprintf(fout, "%s:", alterations.names[node.second - 1].c_str());
			else
				fprintf(fout, "%s\t", alterations.names[node.second - 1].c_str());
			k++;
		}

		k = 0;
		for (size_t j = 0; j < samples.indices.size(); j++)
			if (patientProfile[subNetwork.size() - 1][subgraph.second]->getBit(j) == 1) {
				if (k < patientProfile[subNetwork.size() - 1][subgraph.second]->getSize() - 1)
					fprintf(fout, "%s:", samples.names[j].c_str());
				else
					fprintf(fout, "%s\n", samples.names[j].c_str());
				k++;
			}
		i++;
	}
	fclose(fout);
}


/* MAX COLORFUL SUBNETWORK WITH DEPTH AT LEAST MINPATIENTSUPPORT. */
void solveMaxColorfulNetwork(int minPatientSupport, bool exprout_inclusive_) {
	std::vector<std::unordered_map<colored_node_set, int, colored_node_set_hash>> subNetwork;
	std::vector<std::unordered_map<int, PatientBitmask*>> patientProfile;
	std::unordered_map<colored_node_set, int, colored_node_set_hash> subNetwork_0;
	subNetwork.push_back(subNetwork_0);
	std::unordered_map<int, PatientBitmask*> patientProfile_0;
	patientProfile.push_back(patientProfile_0);

	int color_count[G.V][alterations.indices.size() + 1] = {0};
	for (int j = 0; j < G.V; j++)
		for (size_t i = 0; i < samples.indices.size(); i++)
			for (size_t a = 0; a < alterations.indices.size(); a++)
				if ((geneAlterations[j][i] & (1 << a)) != 0)
					color_count[j][a + 1]++;
	
	int sum = 0;
	for (int j = 0; j < G.V; j++) {
		for (size_t i = 1; i <= alterations.indices.size(); i++)
			if (color_count[j][i] >= minPatientSupport) {
				color_count[j][i] = 1;
				color_count[j][0] = 1;
			}
			else
				color_count[j][i] = 0;
	}
	for (int j = 0; j < G.V; j++)
		if (color_count[j][0] == 1)
			sum++;
	fprintf(stderr, "There are %d nodes where at least %d patients are mutated.\n", sum, minPatientSupport);

	int valid = 0;
	for (int j = 0; j < G.V; j++)
		if (color_count[j][0] == 1)
			for (auto index : G.idxInNeighbourList[j])
				if (color_count[index.first][0] == 1 && index.first > j)
					for (size_t k = 1; k <= alterations.indices.size(); k++)
						for (size_t l = 1; l <= alterations.indices.size(); l++)
							if (color_count[j][k] == 1 && color_count[index.first][l] == 1) {
								PatientBitmask *profile_e_k_l = new PatientBitmask(samples.indices.size());
								colored_node_set edge;
								colored_node node_1 (j, (int)k);
								edge.insert(node_1);
								colored_node node_2 (index.first, (int)l);
								edge.insert(node_2);
								sum = 0;
								for (size_t m = 0; m < samples.indices.size(); m++)
									if ((geneAlterations[j][m] & (1 << (k - 1))) && (geneAlterations[index.first][m] & (1 << (l - 1)))) {
										sum++;
										profile_e_k_l->setBit(m, 1);
									}
								if (sum >= minPatientSupport) {
									auto result = subNetwork[0].insert(std::pair<colored_node_set, int> (edge, valid));
									if (result.second) {
										patientProfile[0][valid] = profile_e_k_l;
										valid++;
									}
								}
							}	
	fprintf(stderr, "There are %lu edges where at least %d patients are mutated at each node.\n", subNetwork[0].size(), minPatientSupport); 

	/* Run The Major Recursion. */
	size_t network_size = 1;
	while (true) {
		std::unordered_map<colored_node_set, int, colored_node_set_hash> nextsubNetwork;
		std::unordered_map<int, PatientBitmask*> nextpatientProfile;
		valid = 0;
		
		for (auto s1 : subNetwork[network_size - 1]) {
			for (auto s2 : subNetwork[0]) {
				colored_node_set candidate = s1.first;
				candidate.insert(s2.first.begin(), s2.first.end());
				if (candidate.size() == network_size + 2) {
					PatientBitmask *profile_candidate = new PatientBitmask(patientProfile[network_size - 1][s1.second]);
					profile_candidate->mergeBitmask(patientProfile[0][s2.second]);
					bool colorful_test = false;
					if (!exprout_inclusive_) {
						std::set<int> color_set;
						for (auto node : candidate)
							color_set.insert(node.second);
						if (color_set.size() >= 2)
							colorful_test = true;
					}
					else {
						int color_count[] = {0, 0, 0, 0, 0};
						for (auto node : candidate)
							color_count[node.second - 1]++;
						for (int i = 0; i < 4; i++)
							if (i != alterations.indices["EXPROUT"])
								color_count[4] += color_count[i];
						if (color_count[4] > 0 && color_count[4] <= 2)
						//if (color_count[4] <= 2)
							colorful_test = true;
					}
					if (colorful_test && (profile_candidate->getSize() >= minPatientSupport)) {
						auto result = nextsubNetwork.insert(std::pair<colored_node_set, int>(candidate, valid));
						if (result.second) {
							nextpatientProfile[valid] = profile_candidate;
							valid++;
						}
					}
				}
			}
		}

		/* Found the largest subnetwork. */
		if (nextsubNetwork.empty()) {
			fprintf(stderr, "The maximum subnetwork size is %lu.\n", network_size + 1);
			break;
		}

		/* Try next level. */
		fprintf(stderr, "There are %lu subnetworks of size %lu, where at least %d patients are mutated at each node.\n", nextsubNetwork.size(), network_size + 2, minPatientSupport);
		subNetwork.push_back(nextsubNetwork);
		patientProfile.push_back(nextpatientProfile);
		network_size++;
	}

	/* Output Solution(s). */
	FILE * fout = fopen("output_colorful.tsv", "w");
	size_t i = 0;
	fprintf(fout, "Solution\t");
	fprintf(fout, "Nodes\t");
	fprintf(fout, "Color\t");
	fprintf(fout, "SampleID\n");
	for (auto subgraph : subNetwork[subNetwork.size() - 1]) {
		fprintf(fout, "Solution_%lu\t", i + 1);
		size_t k = 0;
		for (auto node : subgraph.first) {
			if (k < subgraph.first.size() - 1) 
				fprintf(fout, "%s:", G.nodeNames[node.first].c_str());
			else
				fprintf(fout, "%s\t", G.nodeNames[node.first].c_str());
			k++;
		}
		
		k = 0;
		for (auto node : subgraph.first) {
			if (k < subgraph.first.size() - 1)
				fprintf(fout, "%s:", alterations.names[node.second - 1].c_str());
			else
				fprintf(fout, "%s\t", alterations.names[node.second - 1].c_str());
			k++;
		}

		k = 0;
		for (size_t j = 0; j < samples.indices.size(); j++)
			if (patientProfile[subNetwork.size() - 1][subgraph.second]->getBit(j) == 1) {
				if (k < patientProfile[subNetwork.size() - 1][subgraph.second]->getSize() - 1)
					fprintf(fout, "%s:", samples.names[j].c_str());
				else
					fprintf(fout, "%s\n", samples.names[j].c_str());
				k++;
			}
		i++;
	}
	fclose(fout);
}

int main( int argc, char * argv[] ) {
	printHeader( "MSC-NCI motif" );
	/* INPUT CHECK */
	if (argc <= 1) {
		fprintf(stderr, "./motif -n [network] -l [alteration profiles] -s [maximum subnetwork size] -m [mode] -t [number of working threads]\n\n");
		return 0;
	}
	char consoleFlags[] = {'n', 'l', 's', 'm', 't', 0};
	unordered_map<char, string> consoleParameters;
	for (int i = 1; i < argc; i++) {
		if ( argv[i][0] == '-' && argv[i][1] && i + 1 < argc && argv[i + 1][0] != '-' ) {
			consoleParameters[ argv[i][1] ] = string( argv[i + 1] );
			i++;
		}
	}
	for (char * ptrFlag = consoleFlags; *ptrFlag; ptrFlag++) {
		if ( consoleParameters.find(*ptrFlag) == consoleParameters.end() ) {
			fprintf(stderr, "\n< Error > Missing value for parameter '%c'. Exiting program.\n", *ptrFlag);
			exit(0);
		}
	}

	/* MODE = 0, 1: MAX PAIRWISE SCORE 
	   MODE = 2: MAX SUBGRAPH SIZE WITH MIN SUPPORT 
	   MODE = 3: MAX NUMBER PATIENTS WITH MAX GRAPH SIZE */
	int mode = 0;
	sscanf(consoleParameters['m'].c_str(), "%d", &mode);

	int threshold;
	sscanf(consoleParameters['s'].c_str(), "%d", &threshold);

	int num_threads = 32;
	sscanf(consoleParameters['t'].c_str(), "%d", &num_threads);
	
	printHeader("Reading Input");
	readUndirectedNetwork( consoleParameters['n'].c_str() );
	findConnectedComponents();

	readAlterationProfiles( consoleParameters['l'].c_str() );
	printHeader("Solving the problem");
	
	switch (mode) {
		case 0: 
			runILPSolverMaxNetworkSize(threshold, num_threads);
			break;
		case 1:
			solveMaxNetworkSize(threshold);
			break;
		/* Maximum colorful subnetwork. */
		case 2:
			solveMaxColorfulNetwork(threshold, false);
			break;
		/* Maximum colorful subnetwork with color EXPROUT. */
		case 3:
			solveMaxColorfulNetwork(threshold, true);
			break;
		default: 
			break;
	}

	return 0;
}

