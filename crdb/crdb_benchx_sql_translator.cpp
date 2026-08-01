#include "crdb/crdb_benchx_sql_translator.h"
#include <glog/logging.h>
#include <sstream>
#include <vector>
#include <string>

using namespace std;

// Helper to split string by comma
vector<string> split(const string& s, char delim) {
    vector<string> result;
    stringstream ss(s);
    string item;
    while (getline(ss, item, delim)) {
        result.push_back(item);
    }
    return result;
}

void TranslateBenchXToSQL(pqxx::work& pq_txn, const slog::Transaction& txn) {
    if (!txn.has_code() || txn.code().procedures_size() == 0) return;

    string combined_sql = "";
    


    for (int p_idx = 0; p_idx < txn.code().procedures_size(); ++p_idx) {
        const auto& proc = txn.code().procedures(p_idx);
        if (proc.args_size() == 0) continue;
        string proc_name = proc.args(0);
        
        if (proc_name.substr(0, 4) == "dep_") continue; 



        if (proc_name == "new_order") {
            string cur_w_id = proc.args(1);
            string cur_d_id = proc.args(2);
            string c_id = proc.args(3);
            string cur_dt = proc.args(5);
            
            vector<string> l_ids, supply_w_ids, item_ids, quantities;
            while (p_idx + 1 < txn.code().procedures_size()) {
                const auto& next_proc = txn.code().procedures(p_idx + 1);
                if (next_proc.args_size() == 0) break;
                string next_proc_name = next_proc.args(0);
                if (next_proc_name.substr(0, 4) == "dep_") { p_idx++; continue; }
                if (next_proc_name == "new_order" || next_proc_name == "get_customer_by_name" || next_proc_name == "get_item_by_name" || next_proc_name == "insert_only" || next_proc_name == "delete_only" || next_proc_name == "order_status" || next_proc_name == "stock_level") {
                    break;
                }
                
                l_ids.push_back(next_proc.args(0));
                supply_w_ids.push_back(next_proc.args(1));
                item_ids.push_back(next_proc.args(2));
                quantities.push_back(next_proc.args(3));
                p_idx++;
            }
            
            combined_sql += "WITH dist_update AS (UPDATE district SET d_next_o_id = d_next_o_id + 1 WHERE d_w_id = " + cur_w_id + " AND d_id = " + cur_d_id + " RETURNING d_next_o_id), ";
            combined_sql += "new_o AS (INSERT INTO \"order\" (o_id, o_d_id, o_w_id, o_c_id, o_entry_d, o_carrier_id, o_ol_cnt, o_all_local) SELECT d_next_o_id, " + cur_d_id + ", " + cur_w_id + ", " + c_id + ", '2026-01-01 00:00:00', 0, 15, 1 FROM dist_update RETURNING o_id), ";
            combined_sql += "new_no AS (INSERT INTO new_order (no_o_id, no_d_id, no_w_id) SELECT o_id, " + cur_d_id + ", " + cur_w_id + " FROM new_o RETURNING no_o_id) ";
            
            if (!l_ids.empty()) {
                combined_sql += ", stock_upd AS (UPDATE stock SET s_quantity = s_quantity - x.qty FROM (VALUES ";
                for (size_t i = 0; i < l_ids.size(); i++) {
                    combined_sql += "(" + supply_w_ids[i] + "," + item_ids[i] + "," + quantities[i] + ")";
                    if (i != l_ids.size() - 1) combined_sql += ",";
                }
                combined_sql += ") AS x(w, i, qty) WHERE s_w_id = x.w AND s_i_id = x.i RETURNING s_w_id) ";
                
                combined_sql += "INSERT INTO order_line (ol_o_id, ol_d_id, ol_w_id, ol_number, ol_i_id, ol_supply_w_id, ol_delivery_d, ol_quantity, ol_amount, ol_dist_info) SELECT new_o.o_id, " + cur_d_id + ", " + cur_w_id + ", x.l_id, x.i_id, x.supply_w_id, NULL, x.qty, item.i_price * x.qty, 'dist' FROM new_o CROSS JOIN (VALUES ";
                for (size_t i = 0; i < l_ids.size(); i++) {
                    combined_sql += "(" + l_ids[i] + "," + item_ids[i] + "," + supply_w_ids[i] + "," + quantities[i] + ")";
                    if (i != l_ids.size() - 1) combined_sql += ",";
                }
                combined_sql += ") AS x(l_id, i_id, supply_w_id, qty) JOIN item ON item.i_id = x.i_id; ";
            } else {
                combined_sql += "SELECT 1 FROM new_no; ";
            }
        } else if (proc_name == "get_customer_by_name") {
            vector<string> ws = split(proc.args(1), ',');
            vector<string> ds = split(proc.args(2), ',');
            vector<string> ns = split(proc.args(3), ',');
            if (!ws.empty()) {
                combined_sql += "SELECT c_id FROM customer WHERE (c_w_id, c_d_id, c_last) IN (";
                for (size_t i = 0; i < ws.size(); i++) {
                    combined_sql += "(" + ws[i] + "," + ds[i] + ",'" + ns[i] + "')";
                    if (i != ws.size() - 1) combined_sql += ",";
                }
                combined_sql += "); ";
            }
        } else if (proc_name == "get_item_by_name") {
            vector<string> ws = split(proc.args(1), ',');
            vector<string> ns = split(proc.args(2), ',');
            if (!ns.empty()) {
                combined_sql += "SELECT i_id FROM item WHERE i_name IN (";
                for (size_t i = 0; i < ns.size(); i++) {
                    combined_sql += "'" + ns[i] + "'";
                    if (i != ns.size() - 1) combined_sql += ",";
                }
                combined_sql += "); ";
            }
        } else if (proc_name == "insert_only") {
            vector<string> ws = split(proc.args(1), ',');
            vector<string> ds = split(proc.args(2), ',');
            vector<string> cws = split(proc.args(3), ',');
            vector<string> cds = split(proc.args(4), ',');
            vector<string> cs = split(proc.args(5), ',');
            vector<string> amts = split(proc.args(6), ',');
            vector<string> dts = split(proc.args(7), ',');
            if (!ws.empty()) {
                combined_sql += "INSERT INTO history (h_c_id, h_c_d_id, h_c_w_id, h_d_id, h_w_id, h_date, h_amount, h_data) VALUES ";
                for (size_t i = 0; i < ws.size(); i++) {
                    combined_sql += "(" + cs[i] + "," + cds[i] + "," + cws[i] + "," + ds[i] + "," + ws[i] + ", '2026-01-01 00:00:00' ," + amts[i] + " / 100.0,'data')";
                    if (i != ws.size() - 1) combined_sql += ",";
                }
                combined_sql += "; ";
            }
        } else if (proc_name == "delete_only") {
            vector<string> ws = split(proc.args(1), ',');
            vector<string> ds = split(proc.args(2), ',');
            vector<string> cws = split(proc.args(3), ',');
            vector<string> cds = split(proc.args(4), ',');
            vector<string> cs = split(proc.args(5), ',');
            if (!ws.empty()) {
                combined_sql += "DELETE FROM history WHERE (h_c_w_id, h_c_d_id, h_c_id, h_w_id, h_d_id) IN (";
                for (size_t i = 0; i < ws.size(); i++) {
                    combined_sql += "(" + cws[i] + "," + cds[i] + "," + cs[i] + "," + ws[i] + "," + ds[i] + ")";
                    if (i != ws.size() - 1) combined_sql += ",";
                }
                combined_sql += "); ";
            }
        } else if (proc_name == "order_status") {
            vector<string> ws = split(proc.args(1), ',');
            vector<string> ds = split(proc.args(2), ',');
            vector<string> cs = split(proc.args(3), ',');
            vector<string> os = split(proc.args(4), ',');
            if (!ws.empty()) {
                combined_sql += "SELECT o_entry_d, o_carrier_id, o_ol_cnt FROM \"order\" WHERE (o_w_id, o_d_id, o_id) IN (";
                for (size_t i = 0; i < ws.size(); i++) {
                    combined_sql += "(" + ws[i] + "," + ds[i] + "," + os[i] + ")";
                    if (i != ws.size() - 1) combined_sql += ",";
                }
                combined_sql += "); ";
                combined_sql += "SELECT ol_i_id, ol_supply_w_id, ol_quantity, ol_amount, ol_delivery_d FROM order_line WHERE (ol_w_id, ol_d_id, ol_o_id) IN (";
                for (size_t i = 0; i < ws.size(); i++) {
                    combined_sql += "(" + ws[i] + "," + ds[i] + "," + os[i] + ")";
                    if (i != ws.size() - 1) combined_sql += ",";
                }
                combined_sql += "); ";
            }
        } else if (proc_name == "stock_level") {
            vector<string> ws = split(proc.args(1), ',');
            vector<string> ds = split(proc.args(2), ',');
            vector<string> os = split(proc.args(3), ',');
            vector<string> is;
            if (p_idx + 1 < txn.code().procedures_size()) {
                const auto& items_proc = txn.code().procedures(p_idx + 1);
                for (int k = 0; k < items_proc.args_size(); ++k) {
                    is.push_back(items_proc.args(k));
                }
                p_idx++; // skip the items procedure in the main loop
            }
            if (!ws.empty()) {
                combined_sql += "SELECT d_next_o_id FROM district WHERE (d_w_id, d_id) IN (";
                for (size_t i = 0; i < ws.size(); i++) {
                    combined_sql += "(" + ws[i] + "," + ds[i] + ")";
                    if (i != ws.size() - 1) combined_sql += ",";
                }
                combined_sql += "); ";
                
                combined_sql += "SELECT ol_i_id FROM order_line WHERE ";
                for (size_t i = 0; i < ws.size(); i++) {
                    combined_sql += "(ol_w_id = " + ws[i] + " AND ol_d_id = " + ds[i] + " AND ol_o_id >= " + std::to_string(std::stoi(os[i]) - 20) + " AND ol_o_id < " + os[i] + ")";
                    if (i != ws.size() - 1) combined_sql += " OR ";
                }
                combined_sql += "; ";
            }
            if (!is.empty() && !ws.empty()) {
                combined_sql += "SELECT s_quantity FROM stock WHERE (s_w_id, s_i_id) IN (";
                bool first = true;
                for (size_t i = 0; i < is.size(); i++) {
                    for (size_t k = 0; k < ws.size(); k++) {
                        if (!first) combined_sql += ",";
                        combined_sql += "(" + ws[k] + "," + is[i] + ")";
                        first = false;
                    }
                }
                combined_sql += "); ";
            }

        } else {
            LOG(WARNING) << "Unknown procedure: " << proc_name;
        }
    }

    if (!combined_sql.empty()) {
        pq_txn.exec(combined_sql);
    }
}
