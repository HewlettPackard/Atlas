#include <cassert>
#include <cstring>
#include <iostream>

#include "atlas_api.h"
#include "atlas_alloc.h"

using namespace std;

#define MAXLEN 1024

typedef struct {
  size_t len;
  char str[MAXLEN];
} root_t;

int main (int argc, char const *argv[]) {
  if (argc != 2) {
    cerr << "usage: " << argv[0] << " <name of region>\n";
    return -1;
  }

  NVM_Initialize();

  /* Write to the region */

  int created = 0;
  uint32_t write_region_id = NVM_FindOrCreateRegion(argv[1], O_RDWR, &created);
  if (write_region_id == 100) {
    cerr << "Couldn't create or find persistent region, exiting\n";
    NVM_Finalize();
    return -1;
  }

  if (created) {
    cout << "LOG: Created new region: " << argv[1] << endl;
  }

  char input[MAXLEN];
  cout << "Enter string to be stored persistently: \n";
  cin.getline(input, MAXLEN);

  root_t *root = (root_t *)nvm_alloc(sizeof(root_t), write_region_id);

  root->len = strlen(input);
  strcpy(root->str, input);

  NVM_SetRegionRoot(write_region_id, root);

  NVM_CloseRegion(write_region_id);

  /* Read from the region */

  uint32_t read_region_id = NVM_FindRegion(argv[1], O_RDWR);
  if (read_region_id == 100) {
    cerr << "Couldn't find persistent region, exiting\n";
    NVM_Finalize();
    return -1;
  }

  root_t *newroot = (root_t *)NVM_GetRegionRoot(read_region_id);
  if (!newroot) {
    cerr << "Couldn't read root from the persistent region, exiting" << endl;
    NVM_Finalize();
    return -1;
  }

  if (newroot->len == strlen(newroot->str)) {
    cout << newroot->str << endl;
  } else {
    cerr << "String length mismatch!\n";
  }

  NVM_CloseRegion(read_region_id);

  NVM_Finalize();

  return 0;
}
