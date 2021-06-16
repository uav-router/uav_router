#include <iostream>
#include <regex>
#include <string>

using namespace std;

std::string test_string = "This is ${OUR:-ttt} bag ${THEIR:-kkk}";

std::string get_var(const std::string& name, const std::string& def) {
    return def;
}

std::string expand_vars(std::string& data) {
    static const std::regex ENV{"\\$\\{([^}\\:]+)(\\:\\-([^}]+))?\\}"};
    std::smatch match;
    while (std::regex_search(data, match, ENV)) { 
        for (auto& m : match) {
            cout<<m.str()<<endl;
        }
        cout<<match.str()<<endl;
        cout<<"!=== "<<match.str()<<" - "<<match.size()<<endl;
        data.replace(match.begin()->first, match[0].second, get_var(match[1].str(),match[3].str()));
        //data = match.suffix();
    }
    return data;
}

int main()
{
    
    //cout<<"Hello World";
    cout<<expand_vars(test_string)<<endl;
    
    return 0;
}
