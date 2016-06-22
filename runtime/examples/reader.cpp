#include <cassert>
#include <iostream>
#include <cstring>

#include "atlas_api.h"
#include "atlas_alloc.h"
#include "root_layout.h"

using namespace std;

int main(int argc, char const *argv[]) {
  if (argc != 2) {
    cerr << "usage: ./reader <name of region>\n";
    return -1;
  }

  NVM_Initialize();

  cout << "Read a string\n";
  // XXX does this return NULL on failure? not documented
  uint32_t region_id = NVM_FindRegion(argv[1], O_RDWR);

  assert(region_id);

  root_t *root = (root_t *)NVM_GetRegionRoot(region_id);
  if (!root) {
    std::cerr << "internal error in reading old root, exiting" << std::endl;
    return -1;
  }

  if (root->len == strlen(root->str)) {
    cout << root->str << endl;
  }

  // free the root when done
  nvm_free(root);

  NVM_CloseRegion(region_id);

  NVM_Finalize();
  return 0;
}
