#include <iostream>
#include <cstring>

#include "atlas_api.h"
#include "atlas_alloc.h"
#include "root_layout.h"

using namespace std;

int main(int argc, char const *argv[]) {
  if (argc != 2) {
    cerr << "usage: ./writer <name of region>\n";
    return -1;
  }

  NVM_Initialize();

  uint32_t region_id = NVM_FindOrCreateRegion(argv[1], O_RDWR, NULL);
  if (!region_id) {
    cerr << "Creation of persistent region failed, exiting\n";
    NVM_Finalize();
    return -1;
  }

  char input[MAX_LEN];
  cout << "Enter string to be stored persistently: \n";
  cin.getline(input, MAX_LEN);

  root_t *root = (root_t *)nvm_alloc(sizeof(root_t), region_id);
  cout << "root: " << root << endl;
  NVM_SetRegionRoot(region_id, root);

  //NVM_BEGIN_DURABLE();
  root->len = strlen(input);
  strcpy(root->str, input);
  //NVM_END_DURABLE();

  NVM_CloseRegion(region_id);

  NVM_Finalize();
  return 0;
}
