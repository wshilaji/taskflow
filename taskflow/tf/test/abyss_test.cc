#include "ecm/abyss/abyss.h"

#include "test/test.h"
#include "test/test.pb.h"

using namespace ecm;
using namespace abyss;

template <template <class, class> class ExpParamType>
class MyAbyssExpData : public AbyssExpData {
public:
  template <class T> using ParseSgl = ExpParamType<T, SimpleParser<T>>;

  template <class T> using PbParam = ExpParamType<T, PbParser<T>>;

  ParseSgl<bool> open_adx = {this, "openAdx", false};
  PbParam<test::ScoreConf> score_conf_feeds = {this, "score_conf_feeds"};
};

void abyss_test() {
  MyAbyssExpData<ExpParam> data;

  AbyssIndex index;
  auto status = index.Parse(&data, "test/data/agg_tunnel_snap_list",
                            {"as", "bs", "common"});
  check_status(status);

  std::cout << "=== abyss index ===" << std::endl;
  for (auto msg : index.GetMessages()) {
    std::cout << msg->DebugString() << std::endl;
  }

  MyAbyssExpData<ExpParamRef> data_ref;

  google::protobuf::RepeatedField<int64_t> tunnel_snap_id_list;
  tunnel_snap_id_list.Add(20313);
  tunnel_snap_id_list.Add(21931);
  status = data_ref.Reset(&index, tunnel_snap_id_list);
  check_status(status);

  std::cout << "=== abyss exp ===" << std::endl;
  std::cout << data_ref << std::endl;
}