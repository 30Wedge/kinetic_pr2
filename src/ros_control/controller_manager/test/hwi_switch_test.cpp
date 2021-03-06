///////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2015, Fraunhofer IPA
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright
//     notice, this list of conditions and the following disclaimer in the
//     documentation and/or other materials provided with the distribution.
//   * Neither the name of Fraunhofer IPA nor the names of its
//     contributors may be used to endorse or promote products derived from
//     this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//////////////////////////////////////////////////////////////////////////////

/// \author Mathias Lüdtke

#include <gtest/gtest.h>

#include <map>
#include <set>
#include <string>
#include <algorithm>
#include <iterator>

#include <controller_manager/controller_manager.h>
#include <hardware_interface/robot_hw.h>
#include <hardware_interface/joint_command_interface.h>

namespace hardware_interface {
bool operator<(hardware_interface::ControllerInfo const& i1, hardware_interface::ControllerInfo const& i2)
{
    return i1.name < i2.name;
}
}
template<typename T> T intersect(const T& v1, const T &v2)
{

    std::vector<typename T::value_type> sorted1(v1.begin(), v1.end()), sorted2(v2.begin(), v2.end());
    T res;

    std::sort(sorted1.begin(), sorted1.end());
    std::sort(sorted2.begin(), sorted2.end());

    std::set_intersection(sorted1.begin(), sorted1.end(), sorted2.begin(), sorted2.end(), std::back_inserter(res));
    return res;
}

class SwitchBot : public hardware_interface::RobotHW
{
    hardware_interface::JointStateInterface jsi_;
    hardware_interface::PositionJointInterface pji_;
    hardware_interface::VelocityJointInterface vji_;
    hardware_interface::EffortJointInterface eji_;

    class Joint
    {
        double dummy_;
        std::set<std::string> interfaces_;
        std::string current_;
        hardware_interface::JointStateHandle jsh_;
    public:
        Joint(const std::string &n, hardware_interface::JointStateInterface &iface): jsh_(n, &dummy_, &dummy_, &dummy_)
        {
            iface.registerHandle(jsh_);
        }

        template<typename Interface> void registerHandle(Interface &iface, bool can_switch)
        {
            if(can_switch) interfaces_.insert(hardware_interface::internal::demangledTypeName<Interface>() );
            iface.registerHandle(typename Interface::ResourceHandleType(jsh_, &dummy_));
        }
        bool canSwitch(const std::string &n) const
        {
            if(interfaces_.find(n) == interfaces_.end()) return false;
            return n >= current_;
        }
        void doSwitch(const std::string &n)
        {
            current_ = n;
        }
    };

    std::map<std::string, boost::shared_ptr<Joint> > joints_;

    boost::shared_ptr<Joint> makeJoint(const std::string & name)
    {
        boost::shared_ptr<Joint> j(new Joint(name, jsi_));
        joints_.insert(std::make_pair(name, j));
        return j;
    }

    std::vector<std::string> started_;
    std::vector<std::string> stopped_;
public:
    SwitchBot()
    {
        boost::shared_ptr<Joint> j;

        j = makeJoint("j_pv");

        j->registerHandle(pji_, true);
        j->registerHandle(vji_, true);
        j->registerHandle(eji_, false);

        j = makeJoint("j_pe");
        j->registerHandle(pji_, true);
        j->registerHandle(vji_, false);
        j->registerHandle(eji_, true);

        j = makeJoint("j_ve");
        j->registerHandle(pji_, false);
        j->registerHandle(vji_, true);
        j->registerHandle(eji_, true);

        registerInterface(&pji_);
        registerInterface(&vji_);
        registerInterface(&eji_);

    }

    virtual bool canSwitch(const std::list<hardware_interface::ControllerInfo> &start_list, const std::list<hardware_interface::ControllerInfo> &stop_list) const
    {

        if(!RobotHW::canSwitch(start_list, stop_list))
        {
            ROS_ERROR("Something is wrong with RobotHW");
            return false;
        }

        if(!intersect(start_list, stop_list).empty())
        {
            ROS_ERROR_STREAM("start_list and stop_list intersect");
            return false;
        }

        bool j_pe_e = false;
        bool j_ve_v = false;

        for(std::list<hardware_interface::ControllerInfo>::const_iterator it = start_list.begin(); it != start_list.end(); ++it)
        {
            for (std::set<std::string>::const_iterator res_it = it->resources.begin(); res_it != it->resources.end(); ++res_it)
            {

                // special check
                if(it->hardware_interface == "hardware_interface::EffortJointInterface" && *res_it == "j_pe") j_pe_e = true;
                else if(it->hardware_interface == "hardware_interface::VelocityJointInterface" && *res_it == "j_ve") j_ve_v = true;

                // per joint check
                try
                {
                    if(!joints_.at(*res_it)->canSwitch(it->hardware_interface))
                    {
                        ROS_ERROR_STREAM("Cannot switch " << *res_it << " to " << it->hardware_interface);
                        return false;
                    }
                }
                catch(...)
                {
                    ROS_FATAL("This should never happen!");
                    return false;
                }
            }
        }
        return !(j_pe_e && j_ve_v); // check inter-joint hardware interface conflict
    }
    virtual void doSwitch(const std::list<hardware_interface::ControllerInfo> &start_list, const std::list<hardware_interface::ControllerInfo> &stop_list)
    {
        RobotHW::doSwitch(start_list, stop_list); // check if member is defined

        std::map<std::string, std::string> switches;
        for(std::list<hardware_interface::ControllerInfo>::const_iterator it = stop_list.begin(); it != stop_list.end(); ++it)
        {
            started_.erase(std::remove(started_.begin(), started_.end(), it->name), started_.end());
            stopped_.push_back(it->name);
            for (std::set<std::string>::const_iterator res_it = it->resources.begin(); res_it != it->resources.end(); ++res_it)
            {
                switches[*res_it] = "";
            }
        }
        for(std::list<hardware_interface::ControllerInfo>::const_iterator it = start_list.begin(); it != start_list.end(); ++it)
        {

            stopped_.erase(std::remove(stopped_.begin(), stopped_.end(), it->name), stopped_.end());
            started_.push_back(it->name);
            for (std::set<std::string>::const_iterator res_it = it->resources.begin(); res_it != it->resources.end(); ++res_it)
            {
                switches[*res_it] = it->hardware_interface;
            }
        }
        for(std::map<std::string, std::string>::iterator it = switches.begin(); it != switches.end(); ++it)
        {
            joints_[it->first]->doSwitch(it->second);
        }
    }
    bool checkUnqiue() const
    {
        return intersect(started_, stopped_).empty();
    }
    bool checkNotRunning() const
    {
        return started_.empty();
    }
};


class DummyControllerLoader: public controller_manager::ControllerLoaderInterface
{
    class DummyController : public controller_interface::ControllerBase
    {
        const std::string type_name;
    public:
        DummyController(const std::string &name) : type_name(name) {}
        virtual void update(const ros::Time& /*time*/, const ros::Duration& /*period*/) {}
        virtual bool initRequest(hardware_interface::RobotHW* /*hw*/,
                                 ros::NodeHandle& /*root_nh*/,
                                 ros::NodeHandle &controller_nh,
                                 std::set<std::string>& claimed_resources)
        {
            std::vector<std::string> joints;
            if(!controller_nh.getParam("joints", joints))
            {
                ROS_ERROR("Could not parse joint names");
                return false;
            }
            claimed_resources.insert(joints.begin(), joints.end());
            state_ = INITIALIZED;
            return true;
        }
        virtual std::string getHardwareInterfaceType() const
        {
            return type_name;
        }
    };

    std::map<std::string, std::string> classes;
    void add(const std::string type)
    {
        classes.insert(std::make_pair("Dummy" + type + "Controller", "hardware_interface::" + type));
    }
public:
    DummyControllerLoader() : ControllerLoaderInterface("controller_interface::ControllerBase")
    {
        add("EffortJointInterface");
        add("PositionJointInterface");
        add("VelocityJointInterface");
    }
    virtual boost::shared_ptr<controller_interface::ControllerBase> createInstance(const std::string& lookup_name)
    {
        return boost::shared_ptr<controller_interface::ControllerBase>(new DummyController(classes.at(lookup_name)));
    }
    virtual std::vector<std::string> getDeclaredClasses()
    {
        std::vector<std::string> v;
        for(std::map<std::string, std::string>::iterator it = classes.begin(); it != classes.end(); ++it)
        {
            v.push_back(it->first);
        }
        return v;
    }
    virtual void reload() {}
};

void update(controller_manager::ControllerManager &cm, const ros::TimerEvent& e)
{
    cm.update(e.current_real, e.current_real - e.last_real);
}

class GuardROS
{
public:
    ~GuardROS()
    {
        ros::shutdown();
        ros::waitForShutdown();
    }
};

TEST(SwitchInterfacesTest, SwitchInterfaces)
{
    GuardROS guard;

    SwitchBot bot;
    ros::NodeHandle nh;

    controller_manager::ControllerManager cm(&bot);

    cm.registerControllerLoader(boost::make_shared<DummyControllerLoader>());

    ros::Timer timer = nh.createTimer(ros::Duration(0.01), boost::bind(update, boost::ref(cm), _1));

    ASSERT_TRUE(cm.loadController("group_pos"));
    ASSERT_TRUE(cm.loadController("another_group_pos"));
    ASSERT_TRUE(cm.loadController("group_vel"));
    ASSERT_TRUE(cm.loadController("group_eff"));
    ASSERT_TRUE(cm.loadController("single_pos"));
    ASSERT_TRUE(cm.loadController("single_eff"));
    ASSERT_TRUE(cm.loadController("invalid_group_pos"));
    ASSERT_FALSE(cm.loadController("totally_random_name"));

    {   // test hardware interface conflict
        std::vector<std::string> start, stop;
        start.push_back("invalid_group_pos");
        ASSERT_FALSE(cm.switchController(start, stop, controller_manager_msgs::SwitchControllerRequest::STRICT));
    }
    {   // test resource conflict
        std::vector<std::string> start, stop;
        start.push_back("group_pos");
        start.push_back("group_vel");
        ASSERT_FALSE(cm.switchController(start, stop, controller_manager_msgs::SwitchControllerRequest::STRICT));
    }
    {   // test pos group
        std::vector<std::string> start, stop;
        start.push_back("group_pos");
        ASSERT_TRUE(cm.switchController(start, stop, controller_manager_msgs::SwitchControllerRequest::STRICT));
        ASSERT_TRUE(cm.switchController(stop, start, controller_manager_msgs::SwitchControllerRequest::STRICT)); // clean-up
    }
    {   // test same hardware interface switch
        std::vector<std::string> start, stop, next_start;
        start.push_back("group_pos");
        ASSERT_TRUE(cm.switchController(start, stop, controller_manager_msgs::SwitchControllerRequest::STRICT));

        next_start.push_back("group_pos");
        ASSERT_TRUE(cm.switchController(next_start, start, controller_manager_msgs::SwitchControllerRequest::STRICT));

        ASSERT_TRUE(cm.switchController(stop, next_start, controller_manager_msgs::SwitchControllerRequest::STRICT)); // clean-up
    }
    {   // test same hardware interface switch
        std::vector<std::string> start, stop, next_start;
        start.push_back("group_pos");
        ASSERT_TRUE(cm.switchController(start, stop, controller_manager_msgs::SwitchControllerRequest::STRICT));

        next_start.push_back("another_group_pos");
        ASSERT_TRUE(cm.switchController(next_start, start, controller_manager_msgs::SwitchControllerRequest::STRICT));

        ASSERT_TRUE(cm.switchController(stop, next_start, controller_manager_msgs::SwitchControllerRequest::STRICT)); // clean-up
    }
    {   // test vel group
        std::vector<std::string> start, stop;
        start.push_back("group_vel");
        ASSERT_TRUE(cm.switchController(start, stop, controller_manager_msgs::SwitchControllerRequest::STRICT));
        ASSERT_TRUE(cm.switchController(stop, start, controller_manager_msgs::SwitchControllerRequest::STRICT)); // clean-up
    }
    {   // test eff group
        std::vector<std::string> start, stop;
        start.push_back("group_eff");
        ASSERT_TRUE(cm.switchController(start, stop, controller_manager_msgs::SwitchControllerRequest::STRICT));
        ASSERT_TRUE(cm.switchController(stop, start, controller_manager_msgs::SwitchControllerRequest::STRICT)); // clean-up
    }

    {   // test direct hardware interface upgrades (okay) and downgrades (conflict)
        std::vector<std::string> start, stop, next_start;
        start.push_back("group_eff");
        ASSERT_TRUE(cm.switchController(start, stop, controller_manager_msgs::SwitchControllerRequest::STRICT));

        next_start.push_back("group_vel");
        ASSERT_TRUE(cm.switchController(next_start, start, controller_manager_msgs::SwitchControllerRequest::STRICT)); // upgrade

        ASSERT_FALSE(cm.switchController(start, next_start, controller_manager_msgs::SwitchControllerRequest::STRICT)); // downgrade

        ASSERT_TRUE(cm.switchController(stop, next_start, controller_manager_msgs::SwitchControllerRequest::STRICT)); // clean-up
    }

    {   // test single pos
        std::vector<std::string> start, stop;
        start.push_back("single_pos");
        ASSERT_TRUE(cm.switchController(start, stop, controller_manager_msgs::SwitchControllerRequest::STRICT));
        ASSERT_TRUE(cm.switchController(stop, start, controller_manager_msgs::SwitchControllerRequest::STRICT)); // clean-up
    }
    {   // test single eff
        std::vector<std::string> start, stop;
        start.push_back("single_pos");
        ASSERT_TRUE(cm.switchController(start, stop, controller_manager_msgs::SwitchControllerRequest::STRICT));
        ASSERT_TRUE(cm.switchController(stop, start, controller_manager_msgs::SwitchControllerRequest::STRICT)); // clean-up
    }
    {   // test single pos + group_eff (resource conflict)
        std::vector<std::string> start, stop;
        start.push_back("single_pos");
        start.push_back("group_eff");
        ASSERT_FALSE(cm.switchController(start, stop, controller_manager_msgs::SwitchControllerRequest::STRICT));
    }
    {   // test single pos + group_eff (hardware interface conflict)
        std::vector<std::string> start, stop;
        start.push_back("single_eff");
        start.push_back("group_vel");
        ASSERT_FALSE(cm.switchController(start, stop, controller_manager_msgs::SwitchControllerRequest::STRICT));
    }
    {   // test single pos + group_vel
        std::vector<std::string> start, stop;
        start.push_back("single_pos");
        start.push_back("group_vel");
        ASSERT_TRUE(cm.switchController(start, stop, controller_manager_msgs::SwitchControllerRequest::STRICT));
        ASSERT_TRUE(cm.switchController(stop, start, controller_manager_msgs::SwitchControllerRequest::STRICT)); // clean-up
    }
    {   // test single pos + group_vel + totally_random_name
        std::vector<std::string> start, stop;
        start.push_back("single_pos");
        start.push_back("group_vel");
        start.push_back("totally_random_name");
        ASSERT_FALSE(cm.switchController(start, stop, controller_manager_msgs::SwitchControllerRequest::STRICT));
        ASSERT_FALSE(cm.switchController(stop, start, controller_manager_msgs::SwitchControllerRequest::STRICT));

        ASSERT_TRUE(cm.switchController(start, stop, controller_manager_msgs::SwitchControllerRequest::BEST_EFFORT));
        ASSERT_TRUE(cm.switchController(stop, start, controller_manager_msgs::SwitchControllerRequest::BEST_EFFORT)); // clean-up
    }
    {   // test restart
        std::vector<std::string> start, stop;
        start.push_back("single_pos");
        ASSERT_TRUE(cm.switchController(start, stop, controller_manager_msgs::SwitchControllerRequest::STRICT));

        ASSERT_TRUE(cm.switchController(start, start, controller_manager_msgs::SwitchControllerRequest::STRICT)); // restart
        ASSERT_TRUE(bot.checkUnqiue());

        ASSERT_TRUE(cm.switchController(stop, start, controller_manager_msgs::SwitchControllerRequest::STRICT));  // clean-up
    }
    {   // test stop for controller that is not running
        std::vector<std::string> start, stop;
        stop.push_back("single_pos");
        ASSERT_FALSE(cm.switchController(start, stop, controller_manager_msgs::SwitchControllerRequest::STRICT));
        ASSERT_TRUE(cm.switchController(start, stop, controller_manager_msgs::SwitchControllerRequest::BEST_EFFORT));
    }
    {   // test stop for controller that is not running
        std::vector<std::string> start, stop;
        start.push_back("single_pos");
        ASSERT_TRUE(cm.switchController(start, stop, controller_manager_msgs::SwitchControllerRequest::STRICT));
        ASSERT_FALSE(cm.switchController(start, stop, controller_manager_msgs::SwitchControllerRequest::STRICT));
        ASSERT_TRUE(cm.switchController(start, stop, controller_manager_msgs::SwitchControllerRequest::BEST_EFFORT));
        ASSERT_TRUE(cm.switchController(stop, start, controller_manager_msgs::SwitchControllerRequest::STRICT)); // clean-up
    }
    ASSERT_TRUE(bot.checkNotRunning());
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    ros::init(argc, argv, "controller_manager_switch_test");

    ros::AsyncSpinner spinner(1);
    spinner.start();
    int ret = RUN_ALL_TESTS();
    return ret;
}
