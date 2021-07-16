#include <wayfire/matcher.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/lexer/lexer.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/condition/condition.hpp>
#include <wayfire/view-access-interface.hpp>
#include <wayfire/parser/condition_parser.hpp>

class wf::view_matcher_t::impl
{
  public:
    std::shared_ptr<wf::config::option_t<std::string>> option;

    wf::lexer_t lexer;
    wf::condition_parser_t parser;
    std::shared_ptr<wf::condition_t> condition;

    bool try_parse(const std::string& value, const std::string& opt_name)
    {
        lexer.reset(value);
        try {
            condition = parser.parse(lexer);

            return true;
        } catch (std::runtime_error& error)
        {
            LOGE("Failed to parse condition ", value, " from option ", opt_name);
            LOGE("Reason for the failure: ", error.what());
            condition.reset();
        }

        return false;
    }

    wf::config::option_base_t::updated_callback_t update_condition = [=] ()
    {
        if (!try_parse(option->get_value(), option->get_name()))
        {
            if (option->get_value() != option->get_default_value())
            {
                try_parse(option->get_default_value(),
                    option->get_name() + "(default)");
            }
        }
    };

    void connect_updated_handler()
    {
        if (this->option)
        {
            this->option->add_updated_handler(&update_condition);
        }
    }

    void disconnect_updated_handler()
    {
        if (this->option)
        {
            this->option->rem_updated_handler(&update_condition);
        }
    }

    void set_option(std::shared_ptr<wf::config::option_t<std::string>> option)
    {
        disconnect_updated_handler();
        this->option = option;
        if (option)
        {
            connect_updated_handler();
            update_condition();
        }
    }

    impl() = default;
    ~impl()
    {
        disconnect_updated_handler();
    }

    impl(const impl &) = delete;
    impl(impl &&) = delete;
    impl& operator =(const impl&) = delete;
    impl& operator =(impl&&) = delete;
};

wf::view_matcher_t::view_matcher_t()
{
    this->priv = std::make_unique<impl>();
}

wf::view_matcher_t::view_matcher_t(
    std::shared_ptr<wf::config::option_t<std::string>> option) :
    view_matcher_t()
{
    this->priv->set_option(option);
}

wf::view_matcher_t::view_matcher_t(const std::string& option_name) :
    view_matcher_t()
{
    wf::option_wrapper_t<std::string> option{option_name};
    this->set_from_option(option);
}

void wf::view_matcher_t::set_from_option(
    std::shared_ptr<wf::config::option_t<std::string>> option)
{
    this->priv->set_option(option);
}

bool wf::view_matcher_t::matches(wayfire_view view)
{
    if (this->priv->condition)
    {
        bool ignored = false;
        wf::view_access_interface_t access_interface{view};

        return this->priv->condition->evaluate(access_interface, ignored);
    }

    return false;
}

wf::view_matcher_t::~view_matcher_t() = default;
