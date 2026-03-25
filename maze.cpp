#include <emscripten/emscripten.h>
#include <vector>
#include <stack>
#include <queue>
#include <unordered_map>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <cstring>
using namespace std;

// Wall masking

static const int WALL_N = 1;
static const int WALL_S = 2;
// Use bitflags (powers of two) so walls can be combined with bitwise ops.
static const int WALL_E = 4;
static const int WALL_W = 8;
static const int ALL_WALLS = WALL_N | WALL_S | WALL_E | WALL_W;

//step states returning to JS

static const int STATE_VISITED = 0; //already explored
static const int STATE_FRONTIER = 1; // in queue
static const int STATE_PATH = 2;   // final path
static const int STATE_START = 3;
static const int STATE_END = 4;
// Recursive-backtracker generation (paired FROM → TO, then BACK on pop)
static const int STATE_GEN_FROM = 5;
static const int STATE_GEN_TO = 6;
static const int STATE_GEN_BACK = 7;

// Grid

struct Grid {
    int width = 0;
    int height =0;
    vector<int> cells;

    void init(int w, int h){
        width =w;
        height =h;
        cells.assign(w * h, ALL_WALLS);
    }

    int& at(int row, int col){
        return cells[row * width + col];
    }
    int at(int row, int col) const {
        return cells[row * width + col];
    }
    bool inBounds(int row, int col) const {
        return row >= 0 && row < height && col >= 0 && col < width;
    }
};

static Grid g_grid;

static vector<int> g_steps;

// JS calls this to get a pointer to the step buffer
extern "C" EMSCRIPTEN_KEEPALIVE
int* get_steps_ptr() { return g_steps.data(); }

// JS calls this to get the number of ints in the buffer (steps * 3)
extern "C" EMSCRIPTEN_KEEPALIVE
int get_steps_size() { return static_cast<int>(g_steps.size()); }

// Helper: push one step
static void push_step(int col, int row, int state){
    g_steps.push_back(col);
    g_steps.push_back(row);
    g_steps.push_back(state);
}


// GRID accessor for JS

extern "C" EMSCRIPTEN_KEEPALIVE
int get_width()  { return g_grid.width; }

extern "C" EMSCRIPTEN_KEEPALIVE
int get_height() { return g_grid.height; }

// Returns pointer to the raw cell array so JS can read wall flags directly
extern "C" EMSCRIPTEN_KEEPALIVE
int* get_cells_ptr() { return g_grid.cells.data(); }

// get_cells_ptr gives JS direct read-only access to the raw wall bitmask
// array. The JS code can inspect the wall flags to draw the maze.

extern "C" EMSCRIPTEN_KEEPALIVE
void generate(int width, int height, int seed) {
    srand(seed == 0 ? static_cast<unsigned>(time(nullptr)) : static_cast<unsigned>(seed));

    // Prepare the grid and clear the step buffer
    g_grid.init(width, height);
    g_steps.clear();

    // visited flags: one bool per cell (flat index = row*width + col)
    vector<bool> visited(width * height, false);


    // Directions table encodes row/col offsets and which wall bits to clear
    // on the current cell and on the neighbor when carving a passage.
    struct Dir { int dr, dc, wallCur, wallNbr; };
    static const Dir dirs[4] = {
        {-1, 0, WALL_N, WALL_S },    // North
        {  1,  0, WALL_S, WALL_N },  // South
        {  0,  1, WALL_E, WALL_W },  // East
        {  0, -1, WALL_W, WALL_E },  // West
    };

    stack<pair<int, int>> stk;
    int startRow =0;
    int startCol = 0;
    visited[startRow * width + startCol] = true;
    stk.push({startRow, startCol});

    while (!stk.empty()) {
        auto [row, col] = stk.top();

        vector<int> available;
        for(int i=0; i< 4; i++){
            int nr = row + dirs[i].dr;
            int nc = col + dirs[i].dc;
            if (g_grid.inBounds(nr, nc) && !visited[nr * width + nc]){
                available.push_back(i);
            }
            
        }
        if (!available.empty()) {
            // Choose a random unvisited neighbor and carve a connection.
            int chosen = available[rand() % available.size()];
            int nr = row + dirs[chosen].dr;
            int nc = col + dirs[chosen].dc;

            push_step(col, row, STATE_GEN_FROM);

            // Carve passage between (row,col) and (nr,nc).
            // We clear the wall bit on *both* cells using bitwise AND with
            // the inverse of the wall flag. Example: to remove the North
            // wall we do: cell &= ~WALL_N.
            g_grid.at(row, col) &= ~dirs[chosen].wallCur;
            g_grid.at(nr,  nc ) &= ~dirs[chosen].wallNbr;

            // Mark neighbor visited and push it on the stack (move forward)
            visited[nr * width + nc] = true;
            stk.push({nr, nc});
            push_step(nc, nr, STATE_GEN_TO);
        } else {
            // No unvisited neighbors: backtrack to previous cell
            stk.pop();
            push_step(col, row, STATE_GEN_BACK);
        }
    }
    // Emit start/end markers so the JS UI knows the generation finished
    // and where the user-visible start/end cells are.
    push_step(startCol, startRow, STATE_START);
    push_step(width - 1, height - 1, STATE_END);
}

// ─────────────────────────────────────────────
//  Shared: reconstruct path from parent map
// ─────────────────────────────────────────────

static void reconstruct_path(
    const std::unordered_map<int,int>& parent,
    int startIdx, int endIdx, int width)
{
    // Reconstruct the path from endIdx back to startIdx using the
    // parent map recorded during search. The parent map maps a cell
    // index (row*width + col) to the previous cell on the path.
    std::vector<int> path;
    int cur = endIdx;
    // Walk backwards from the end to the start following parent links.
    while (cur != startIdx) {
        path.push_back(cur);
        cur = parent.at(cur); // .at throws if key missing — parent should contain the chain
    }
    path.push_back(startIdx);
    // Reverse so we go from start -> end.
    std::reverse(path.begin(), path.end());

    // Emit the PATH state for each cell along the final path so JS can
    // highlight it in the UI.
    for (int idx : path) {
        int c = idx % width; // column
        int r = idx / width; // row
        push_step(c, r, STATE_PATH);
    }
}

// Neighbor helper: returns reachable neighbors (no wall between them)
static std::vector<int> get_neighbors(const Grid& grid, int row, int col) {
    struct Dir { int dr, dc, wall; };
    static const Dir dirs[4] = {
        { -1,  0, WALL_N },
        {  1,  0, WALL_S },
        {  0,  1, WALL_E },
        {  0, -1, WALL_W },
    };

    std::vector<int> nbrs;
    for (auto& d : dirs) {
        int nr = row + d.dr;
        int nc = col + d.dc;
        // A neighbor is reachable if (a) it's inside the grid, and
        // (b) there is no wall between the current cell and that neighbor.
        // We check the current cell's wall bit: if the wall bit is NOT set
        // (ie. grid.at(row,col) & d.wall == 0) then movement is possible.
        if (grid.inBounds(nr, nc) && !(grid.at(row, col) & d.wall))
            nbrs.push_back(nr * grid.width + nc);
    }
    return nbrs;
}


// Solve the algorithm using BFS

extern "C" EMSCRIPTEN_KEEPALIVE
void solve_bfs(int startCol, int startRow, int endCol, int endRow) {
    g_steps.clear();

    int width = g_grid.width;
    int startIdx = startRow * width + startCol;
    int endIdx = endRow * width + endCol;

    unordered_map<int, int> parent;
    vector<bool> visited(width * g_grid.height, false);
    queue<int> q;

    //start the search
    visited[startIdx] = true;
    q.push(startIdx);
    push_step(startCol, startRow, STATE_START);
    push_step(endCol, endRow, STATE_END);

    bool found = false;
    while(!q.empty() && !found){
        int cur = q.front(); q.pop();
        int cr = cur / width, cc = cur % width;

        push_step(cc,cr, STATE_VISITED);

    // get_neighbors expects (grid, row, col). Use the correct global grid
    // (`g_grid`) and the current row/col (cr, cc).
    for (int nbr : get_neighbors(g_grid, cr, cc)) {
            if(!visited[nbr]) {
                visited[nbr] = true;
                parent[nbr] = cur;
                push_step(nbr % width, nbr / width, STATE_FRONTIER);
                q.push(nbr);
                if (nbr == endIdx) {
                    found= true; 
                    break;
                }
            }
        }
    }

    if (found) reconstruct_path(parent, startIdx, endIdx, width);
}