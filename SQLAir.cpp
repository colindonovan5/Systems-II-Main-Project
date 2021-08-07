/*
 * A very lightweight (light as air) implementation of a simple CSV-based 
 * database system that uses SQL-like syntax for querying and updating the
 * CSV files.
 * Copyright 2021 Colin Donovan
 */

#include <string>
#include <fstream>
#include <tuple>
#include <algorithm>
#include "SQLAir.h"
#include "HTTPFile.h"

/**
 * A fixed HTTP response header that is used by the runServer method below.
 * Note that this a constant (and not a global variable)
 */
const std::string HTTPRespHeader = "HTTP/1.1 200 OK\r\n"
        "Server: localhost\r\n"
        "Connection: Close\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: ";


/**
 * Handle a wildcard operator
 */
void handleWildcard(CSV& csv, StrVec& colNames) {
    if (colNames.size() == 1 && colNames.front() == "*") {
        // With a wildcard column name, we print all of the columns in CSV
        colNames = csv.getColumnNames();
    }
}

/**
 * Helper method for selectQuery
 * @param csv The csv
 * @param mustWait If query starts with wait or not
 * @param colNames The column names
 * @param whereColIdx The column id
 * @param cond The condition
 * @param value The value
 * @param os The output stream
 * @param rows The vector of strings to add the matches to.
 * @param numSel The number selected rows
 * @return Whether any queries were selected or not
 */
bool SQLAir::getQueriesFromCSV(CSV& csv, bool mustWait, StrVec colNames,
        const int whereColIdx, const std::string& cond,
        const std::string& value, std::ostream& os, StrVec& rows, int& numSel) {
    for (auto& row : csv) {
        // Determine if this row matches "where" clause condition, if any
        const bool isMatch = (whereColIdx == -1) ? true :
                matches(row.at(whereColIdx), cond, value);
        if (isMatch) {
            std::string delim = "";
            for (const auto& colName : colNames) {
                // Lock at each column then push back a matching row
                std::unique_lock<std::mutex> lock(row.rowMutex);
                rows.push_back(delim + row.at(csv.getColumnIndex(colName)));
                delim = "\t";
            }
            rows.push_back("\n");
            numSel++;
        }
    }
    
    // Return true if there are any rows that match
    if (numSel == 0) {
        return false;
    } 
    
    return true;
}
// API method to perform operations associated with a "select" statement
// to print columns that match an optional condition.

void SQLAir::selectQuery(CSV& csv, bool mustWait, StrVec colNames,
        const int whereColIdx, const std::string& cond,
        const std::string& value, std::ostream& os) {
    // Convert any "*" to suitable column names.
    handleWildcard(csv, colNames);
    // First print the column names.
    // Print each row that matches an optional condition.
    int numSelects = 0;
    StrVec rows;
    if (mustWait) {
        // Lock and then wait until anything is selected
        std::unique_lock<std::mutex> lock(csv.csvMutex);
        while (!getQueriesFromCSV(csv, mustWait, colNames, 
                whereColIdx, cond, value, os, rows, numSelects)) {
            csv.csvCondVar.wait(lock);          
        }
    } else {
        // Lock and then select
        std::unique_lock<std::mutex> lock(csv.csvMutex);
        getQueriesFromCSV(csv, mustWait, colNames, whereColIdx, cond, value, os,
            rows, numSelects);
    }

    // Print the rows if there are results
    if (numSelects > 0) os << colNames << std::endl;

    // Print all of the matching rows
    for (std::string row : rows) {
        os << row;
    }

    os << numSelects << " row(s) selected.\n";
}

/**
 * Helper method for updateQuery
 * @param csv The csv
 * @param mustWait If query starts with wait or not
 * @param colNames The column names
 * @param values The values of the update query
 * @param whereColIdx The column id
 * @param cond The condition
 * @param value The value
 * @param os The output stream
 * @param numSelects The number selected rows
 * @return Whether any queries were updated or not
 */
bool SQLAir::updateQueriesInCSV(CSV& csv, bool mustWait, StrVec colNames, 
        StrVec values, const int whereColIdx, const std::string& cond,
        const std::string& value, std::ostream& os, int& numSelects) {
    for (auto& row : csv) {
        // Check for a match, if it's a where or if there is no where clause
        const bool isMatch = (whereColIdx == -1) ? true :
            matches(row.at(whereColIdx), cond, value);
        if (isMatch) {
        // In the row, update values for each column specified by the user
            for (size_t i = 0; (i < colNames.size()); i++) {
                // Get the index number of the column the user want's 
                // to update
                std::unique_lock<std::mutex> lock(row.rowMutex);
                const int colIdx = csv.getColumnIndex(colNames.at(i));

                // Update the corresponding column-value in the current row
                row.at(colIdx) = values.at(i);
            }
            numSelects++;
        }
    }
    // If there are no results, return false.
    if (numSelects == 0) {
        return false;
    } 
    return true;
}

/**
 * The method to run an update query
 * @param csv The csv
 * @param mustWait If query starts with wait or not
 * @param colNames The column names
 * @param values The values of the update query
 * @param whereColIdx The column id
 * @param cond The condition
 * @param value The value
 * @param os The output stream
 */
void
SQLAir::updateQuery(CSV& csv, bool mustWait, StrVec colNames, StrVec values,
        const int whereColIdx, const std::string& cond,
        const std::string& value, std::ostream& os) {
    // Update each row that matches an optional condition.

    // This is a partial implementation to show the simple logic of updating
    // user-specified columns in a given row(s).  You can further modify the
    // implementation as you see fit.
    int numSelects = 0;
        // If the wait flag is set to true, wait until the query works.
        if (mustWait) {
            std::unique_lock<std::mutex> lock(csv.csvMutex);
            while (!updateQueriesInCSV(csv, mustWait, colNames, values,
                    whereColIdx, cond, value, os, numSelects)) {
                // Wait until a query is updated
                csv.csvCondVar.wait(lock);
                csv.csvCondVar.notify_all();
            }
        } else {
            // Lock and then update, even if there's no result.
            std::unique_lock<std::mutex> lock(csv.csvMutex);
            updateQueriesInCSV(csv, mustWait, colNames, values,
                    whereColIdx, cond, value, os, numSelects);
            csv.csvCondVar.notify_all();
        }
        
    os << numSelects << " row(s) updated." << std::endl;
}

void
SQLAir::insertQuery(CSV& csv, bool mustWait, StrVec colNames,
        StrVec values, std::ostream& os) {
    throw Exp("insert is not yet implemented.");
}

void
SQLAir::deleteQuery(CSV& csv, bool mustWait, const int whereColIdx,
        const std::string& cond, const std::string& value, std::ostream& os) {
    throw Exp("delete is not yet implemented.");
}

//-------------------------------------------------------------------------

// Convenience helper method to return the CSV object for a given
// file or URL.

CSV& SQLAir::loadAndGet(std::string fileOrURL) {
    // Check if the specified fileOrURL is already loaded in a thread-safe
    // manner to avoid race conditions on the unordered_map
    {
        std::lock_guard<std::mutex> guard(recentCSVMutex);
        // Use recent CSV if parameter was empty string.
        fileOrURL = (fileOrURL.empty() ? recentCSV : fileOrURL);
        // Update the most recently used CSV for the next round
        recentCSV = fileOrURL;
        if (inMemoryCSV.find(fileOrURL) != inMemoryCSV.end()) {
            // Requested CSV is already in memory. Just return it.
            return inMemoryCSV.at(fileOrURL);
        }
    }
    // When control drops here, we need to load the CSV into memory.
    // Loading or I/O is being done outside critical sections
    CSV csv;  // Load data into this csv
    if (fileOrURL.find("http://") == 0) {
        // This is an URL. We have to get the stream from a web-server
        // Implement this feature.
        std::string host, port, path;
        std::tie(host, port, path) = Helper::breakDownURL(fileOrURL);
        // Use helper method to load the data from a given URL. The method
        // below may throw exceptions on errors.
        loadFromURL(csv, host, port, Helper::url_decode(path));
    } else {
        // We assume it is a local file on the server. Load that file.
        std::ifstream data(fileOrURL);
        // This method may throw exceptions on errors.
        csv.load(data);
    }

    // We get to this line of code only if the above if-else to load the
    // CSV did not throw any exceptions. In this case we have a valid CSV
    // to add to our inMemoryCSV list. We need to do that in a thread-safe
    // manner.
    std::lock_guard<std::mutex> guard(recentCSVMutex);
    // Move (instead of copy) the CSV data into our in-memory CSVs
    inMemoryCSV[fileOrURL].move(csv);
    // Return a reference to the in-memory CSV (not temporary one)
    return inMemoryCSV.at(fileOrURL);
}

// Save the currently loaded CSV file to a local file.

void
SQLAir::saveQuery(std::ostream& os) {
    if (recentCSV.empty() || recentCSV.find("http://") == 0) {
        throw Exp("Saving CSV to an URL using POST is not implemented");
    }
    // Create a local file and have the CSV write itself.
    std::ofstream csvData(recentCSV);
    inMemoryCSV.at(recentCSV).save(csvData);
    os << recentCSV << " saved.\n";
}

//--------------------[  HTTP/web related methods  ]-------------------

// Convenience namespace to streamline the code below.
using namespace boost::asio;
using namespace boost::asio::ip;

// This method is called from a separate thread to process a single
// HTTP request from a web-client

void
SQLAir::clientThread(TcpStreamPtr client) {
    // Extract the SQL query from the first line for processing
    std::string req;
    *client >> req >> req;
    // Skip over all the HTTP request headers. Without this loop the 
    // web-server will not operate correctly with all the web-browsers
    for (std::string hdr; (std::getline(*client, hdr) && !hdr.empty() &&
            hdr != "\r");) {
    }

    // URL-decode the request to translate special/encoded characters
    req = Helper::url_decode(req);
    // Check and do the necessary processing based on type of request
    const std::string prefix = "/sql-air?query=";
    if (req.find(prefix) != 0) {
        // This is request for a data file. So send the data file out.
        *client << http::file("./" + req);
    } else {
        // This is a sql-air query. Let's have the helper method do the 
        // processing for us
        std::ostringstream os;
        try {
            std::string sql = Helper::trim(req.substr(prefix.size()));
            if (sql.back() == ';') {
                sql.pop_back();  // Remove trailing semicolon.
            }
            process(sql, os);
        } catch (const std::exception &exp) {
            os << "Error: " << exp.what() << std::endl;
        }
        // Send HTTP response back to the client.
        const std::string resp = os.str();
        // Send response back to the client.
        *client << HTTPRespHeader << resp.size() << "\r\n\r\n" << resp;
    }
    // Reduce number of threads when the thread stops and notify that 
    // the thread is done
    SQLAir::numThreads--;
    SQLAir::thrCond.notify_all();}

// The method to have this class run as a web-server. 
void
SQLAir::runServer(boost::asio::ip::tcp::acceptor& server, const int maxThr) {
    for (bool done = false; !done;) {
        // Creates garbage-collected connection on heap 
        TcpStreamPtr client = std::make_shared<tcp::iostream>();
        // Wait for a client to connect
        server.accept(*client->rdbuf());
        // Now we have a I/O stream to talk to the client. 
        // Lock the mutex
        std::unique_lock<std::mutex> lock(SQLAir::recentCSVMutex);
        std::thread thr(&SQLAir::clientThread, this, client);
        // Increase the number of threads, spin it up, and wait if there are 
        // max number of threads
        SQLAir::numThreads++;
        SQLAir::thrCond.wait(lock, 
                [maxThr, this]{ return numThreads < maxThr; });
        thr.detach();  // Run independently
    }
}

void
SQLAir::loadFromURL(CSV& csv, const std::string& hostName,
        const std::string& port, const std::string& path) {
    // Setup a boost tcp stream to send an HTTP request to the web-server
    tcp::iostream client(hostName, port);
    if (!client.good()) {
        throw Exp("Unable to connect to " + hostName + " at port " + port);
    }
    // Send an HTTP get request to get the data from the server
    client << "GET " << path << " HTTP/1.1\r\nHost: " << hostName << "\r\n"
            << "Connection: Close\r\n\r\n";

    // Get response status from server to ensure we have a valid response.
    std::string status;  // To ensure it is 200 OK status code.
    std::getline(client, status);
    // Read and skip over HTTP response headers
    for (std::string hdr; std::getline(client, hdr) && !hdr.empty()
            && hdr != "\r";) {
    }
    // Double-check everything worked correctly so far...
    if (!client.good() || (status.find("200 OK") == std::string::npos)) {
        throw Exp("Error (" + Helper::trim(status) + ") getting " + path +
                " from " + hostName + " at port " + port);
    }
    // Now have the CSV class do rest of the processing
    csv.load(client);
}
