/**
 *  This module is about selecting the numerical and memory domains, initiating
 *  the verification process and returning the results.
 **/
#include <inttypes.h>
#include <assert.h>

#include <vector>
#include <string>
#include <functional>
#include <tuple>
#include <map>
#include <ctime>
#include <iostream>

#include <boost/signals2.hpp>

#include "crab/base_property.hpp"
#include "crab/assertion.hpp"
#include "crab/checker.hpp"
#include "crab/assumptions.hpp"

#include "config.hpp"
#include "asm_cfg.hpp"

#include "crab_domains.hpp"
#include "crab_common.hpp"
#include "crab_constraints.hpp"
#include "crab_verifier.hpp"


using std::string;
using std::vector;
using std::map;

using printer_t = boost::signals2::signal<void(const string&)>;

using namespace crab::cfg;
using namespace crab::checker;
using namespace crab::analyzer;
using namespace crab::domains;
using namespace crab::domain_impl;

static checks_db analyze(string domain_name, cfg_t& cfg, printer_t& pre_printer, printer_t& post_printer);

static vector<string> sorted_labels(cfg_t& cfg)
{
    vector<string> labels;
    for (const auto& block : cfg)
        labels.push_back(block.label());

    std::sort(labels.begin(), labels.end(), [](string a, string b){
        if (first_num(a) < first_num(b)) return true;
        if (first_num(a) > first_num(b)) return false;
        return a < b;
    });
    return labels;
}

std::tuple<bool, double> abs_validate(Cfg const& simple_cfg, string domain_name, program_info info)
{
    variable_factory_t vfac;
    cfg_t cfg(entry_label(), ARR);
    build_crab_cfg(cfg, vfac, simple_cfg, info);

    printer_t pre_printer;
    printer_t post_printer;
    
    using namespace std;
    clock_t begin = clock();

    checks_db checks = analyze(domain_name, cfg, pre_printer, post_printer);

    clock_t end = clock();
    double elapsed_secs = double(end - begin) / CLOCKS_PER_SEC;

    int nwarn = checks.get_total_warning() + checks.get_total_error();
    if (global_options.print_invariants) {
        for (string label : sorted_labels(cfg)) {
            pre_printer(label);
            cfg.get_node(label).write(crab::outs());
            post_printer(label);
        }
    }

    if (nwarn > 0) {
        if (global_options.print_failures) {
            checks.write(crab::outs());
        }
        return {false, elapsed_secs};
    }
    return {true, elapsed_secs};
}

template<typename analyzer_t>
static auto extract_pre(analyzer_t& analyzer)
{
    map<string, typename analyzer_t::abs_dom_t> res;
    for (const auto& block : analyzer.get_cfg())
        res.emplace(block.label(), analyzer.get_pre(block.label()));
    return res;
}

template<typename analyzer_t>
static auto extract_post(analyzer_t& analyzer)
{
    map<string, typename analyzer_t::abs_dom_t> res;
    for (const auto& block : analyzer.get_cfg())
        res.emplace(block.label(), analyzer.get_post(block.label()));
    return res;
}

template<typename analyzer_t>
static checks_db check(analyzer_t& analyzer)
{
    int verbose = global_options.print_failures ? 2 : 0;
    using checker_t = intra_checker<analyzer_t>;
    using prop_checker_ptr = typename checker_t::prop_checker_ptr;
    checker_t checker(analyzer, {
        prop_checker_ptr(new assert_property_checker<analyzer_t>(verbose))
    });
    checker.run();
    return checker.get_all_checks();
}

static checks_db dont_analyze(cfg_t& cfg, printer_t& printer, printer_t& post_printer)
{
    return {};
}

template<typename dom_t, typename analyzer_t>
void check_semantic_reachability(cfg_t& cfg, analyzer_t& analyzer, checks_db& c)
{
    for (auto& b : cfg) {
        dom_t post = analyzer.get_post(b.label());
        if (post.is_bottom()) {
            if (b.label().find(':') == string::npos)
                c.add(_ERR, {"unreachable", (unsigned int)first_num(b.label()), 0});
        }
    }
}

template<typename dom_t>
static checks_db analyze(cfg_t& cfg, printer_t& pre_printer, printer_t& post_printer)
{
    dom_t::clear_global_state();
    using analyzer_t = intra_fwd_analyzer<cfg_ref<cfg_t>, dom_t>;
    
    liveness<typename analyzer_t::cfg_t> live(cfg);
    if (global_options.liveness) {
        live.exec();
    }

    analyzer_t analyzer(cfg, dom_t::top(), &live);
    
    analyzer.run();

    if (global_options.print_invariants) {
        pre_printer.connect([pre=extract_pre(analyzer)](const string& label) {
            dom_t inv = pre.at(label);
            crab::outs() << "\n" << inv << "\n";
        });
        post_printer.connect([post=extract_post(analyzer)](const string& label) {
            dom_t inv = post.at(label);
            crab::outs() << "\n" << inv << "\n";
        });
    }

    checks_db c = check(analyzer);
    if (global_options.check_semantic_reachability) {
        check_semantic_reachability<dom_t>(cfg, analyzer, c);
    }
    return c;
}

struct domain_desc {
    std::function<checks_db(cfg_t&, printer_t&, printer_t&)> analyze;
    string description;
};

// ELINA_DOMAINS / APRON_DOMAINS are defined in compiler invocation
const map<string, domain_desc> domains{
    { "zoneCrab" , { analyze<array_expansion_domain<z_sdbm_domain_t>>, "zone (crab. split dbm, safe)" } },
    { "none"     , { dont_analyze, "build CFG only, don't perform analysis" } },
};

map<string, string> domain_descriptions()
{
    map<string, string> res;
    for (auto const [name, desc] : domains)
        res.emplace(name, desc.description);
    return res;
}

static checks_db analyze(string domain_name, cfg_t& cfg, printer_t& pre_printer, printer_t& post_printer)
{
    checks_db res = domains.at(domain_name).analyze(cfg, pre_printer, post_printer);
    return res;
}
