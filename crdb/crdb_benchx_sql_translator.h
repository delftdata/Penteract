#pragma once

#include <pqxx/pqxx>
#include "proto/transaction.pb.h"

void TranslateBenchXToSQL(pqxx::work& pq_txn, const slog::Transaction& txn);
