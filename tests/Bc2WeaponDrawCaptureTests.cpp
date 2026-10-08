#include "Bc2ReloadDrawCapture.h"
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace fvr::bc2;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
int main(){
 {Bc2ReloadDrawCapture capture;CHECK(!capture.Enable(false,100));CHECK(!capture.Enable(true,0));CHECK(capture.Enable(true,100));CHECK(!capture.Enable(true,100));CHECK(capture.Stop());std::ostringstream report;capture.Report(report);CHECK(report.str().find("catalog_native_association_verified\":false")!=std::string::npos);}
 {Bc2ReloadDrawCapture capture;std::vector<WeaponDrawSection> catalog(LegacyReloadDrawCatalog().begin(),LegacyReloadDrawCatalog().end());catalog[0].resource="copied\"source.res";CHECK(capture.Enable(true,100,catalog));catalog.clear();CHECK(capture.Stop());std::ostringstream report;capture.Report(report);CHECK(report.str().find("copied\\\"source.res")!=std::string::npos);}
 {Bc2ReloadDrawCapture capture;std::vector<WeaponDrawSection> catalog;CHECK(!capture.Enable(true,100,catalog));catalog.assign(LegacyReloadDrawCatalog().begin(),LegacyReloadDrawCatalog().end());catalog.push_back(catalog[0]);CHECK(!capture.Enable(true,100,catalog));CHECK(capture.Enable(true,100));CHECK(capture.Stop());}
 std::cout<<"3 CPU-only draw capture admission/report groups passed (no device created)\n";
}
