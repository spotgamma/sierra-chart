#include "sierrachart.h"
#include <string>
#include <sstream>
#include <vector>
SCDLLName("SpotGamma Levels Importer")

/*

    The format of the CSV is as follows:
        (float)  Price
        (string) Note
        (string) Color
        
*/

// Struct we'll use to reference our vector
struct PriceLabel {
    float Price;
    SCString Label;
    COLORREF LabelColor;
};

SCSFExport scsf_SpotGammaLevels(SCStudyInterfaceRef sc)
{
    const SCString FAKE_URL = "https://api.spotgamma.com/integration/FAKE_URL/SPX_sierra.csv";
    
    // logging object
    SCString msg;

    // General Input Settings
    SCInputRef UrlPath = sc.Input[0];
    SCInputRef DrawLinesOnChart = sc.Input[2];
    SCInputRef ShowPriceOnChart = sc.Input[3];
    // Update Interval Input (in minutes)
    SCInputRef UpdateInterval = sc.Input[4];
    
    // Set configuration variables
    if (sc.SetDefaults)
    {
        sc.GraphName = "SpotGamma Levels";
        sc.GraphRegion = 0;

        UrlPath.Name = "CSV URL";
        // example template:
        // https://docs.google.com/spreadsheets/d/1fx_Ym6QodEJIqQO90HR1dWYFw5RoK2w5ywtHeMlMeZY/gviz/tq?tqx=out:csv
        // Google Docs Spreadsheet
        // REQUIRED:
        //    - set SHARING to "Anyone with link"
        //    - strip off anything after the unique key, (eg "/edit#usp=sharing")
        UrlPath.SetString(FAKE_URL);

        // Chart specific settings
        DrawLinesOnChart.Name = "Draw Lines?";
        DrawLinesOnChart.SetYesNo(1);

        ShowPriceOnChart.Name = "Show Price Label?";
        ShowPriceOnChart.SetYesNo(0);

        UpdateInterval.Name = "Update Interval (in Min, 0 = manual update with INS key)";
        UpdateInterval.SetInt(0);

        return;
    }

    // Process to initialize variables and pointers along with StateMessage for debug
    int UpdateIntervalInMin = UpdateInterval.GetInt();
    int UpdateIntervalInSec = UpdateIntervalInMin * 60;
    int &LastUpdated = sc.GetPersistentInt(9);
    int& RequestState = sc.GetPersistentInt(1);
	enum {REQUEST_NOT_SENT = 0,  REQUEST_SENT, REQUEST_RECEIVED};
	SCString StateMessage; // Used for debugging purposes.  Should add "if enabled" logic toggle in the future.

    // Only run at time interval
    // Convert TimeInSec to Seconds and compare to last actual update
    SCDateTime Now = sc.CurrentSystemDateTime;
    int TimeInSec = Now.GetTimeInSeconds();
    
    if (LastUpdated > 0 && LastUpdated + UpdateIntervalInSec > TimeInSec && sc.Index != 0 && RequestState != REQUEST_SENT) {
        // exit function if nothing to update
        return;
    }
    LastUpdated = TimeInSec;


    // hold our HTTP Response
    // Reference to persistent SCString for storing or accessing HTTP response content
    SCString &HttpResponseContent = sc.GetPersistentSCString(4);

    SCString Url = UrlPath.GetString();
    // add specific code to output as CSV attachment from Google Sheets
    // Reference to how to get Google Sheets to output an editable CSV file: 
    // https://www.megalytic.com/knowledge/using-google-sheets-to-host-editable-csv-files
    // Syntax is such: /gvis/tq?tqx=out:csv
    if (Url == FAKE_URL) {
        sc.AddMessageToLog("Please input the proper Integration URL", 0);
        return;
    }
    Url.Format("%s/gviz/tq?tqx=out:csv", Url.GetChars());

    // Cast the pointer retrieved from persistent storage to a pointer to a vector of PriceLabel objects
    std::vector<PriceLabel>* p_PriceLabels = reinterpret_cast<std::vector<PriceLabel>*>(sc.GetPersistentPointer(0));
    if (p_PriceLabels == NULL) {
        p_PriceLabels = new std::vector<PriceLabel>;
        sc.SetPersistentPointer(0, p_PriceLabels);
    }
    else {
        // clear old levels
        p_PriceLabels->clear();
    }

    
    // HTTP Request Section with StateMessage for debug

    // latest request status
    

	
    if ((sc.Index == 0 || UpdateIntervalInMin > 0) && RequestState == REQUEST_NOT_SENT) {
		// StateMessage.Format("sc.Index: %d, UpdateIntervalInMin: %d, RequestState: %d", sc.Index, UpdateIntervalInMin, RequestState);
		// sc.AddMessageToLog(StateMessage, 0);
        if (!sc.MakeHTTPRequest(Url)) {
            sc.AddMessageToLog("Error making HTTP request.", 1);
            RequestState = REQUEST_NOT_SENT;
        } else {
			sc.AddMessageToLog("HTTP Request SENT.", 1);
			// StateMessage.Format("sc.Index: %d, UpdateIntervalInMin: %d, RequestState: %d", sc.Index, UpdateIntervalInMin, RequestState);
			// sc.AddMessageToLog(StateMessage, 0);
            RequestState = REQUEST_SENT;
            // sc.AddMessageToLog(sc.HTTPResponse, 1); // HTTP response for debugging
            HttpResponseContent = sc.HTTPResponse;
        }
    }


    if (RequestState == REQUEST_SENT) {
        // StateMessage.Format("sc.Index: %d, UpdateIntervalInMin: %d, RequestState: %d", sc.Index, UpdateIntervalInMin, RequestState);
	    // sc.AddMessageToLog(StateMessage, 0);
        if (sc.HTTPResponse != "") {
            RequestState = REQUEST_RECEIVED;
			sc.AddMessageToLog("HTTP Request RECEIVED, Updating Levels", 1);
            // sc.AddMessageToLog(sc.HTTPResponse, 1); // HTTP response for debugging
            HttpResponseContent = sc.HTTPResponse;
        } else {
        // Request not completed, nothing to do
			// sc.AddMessageToLog("Hanging Request", 0);
        return;
        }
    }

    // reset state for next run
    RequestState = REQUEST_NOT_SENT;
	// StateMessage.Format("sc.Index: %d, UpdateIntervalInMin: %d, RequestState: %d", sc.Index, UpdateIntervalInMin, RequestState);
	// sc.AddMessageToLog(StateMessage, 0);
    // sc.AddMessageToLog("RequestState Reset: REQUEST_NOT_SENT", 0);

    // Tokenize
    // Initialize vector to store tokens from CSV
    std::vector<char*> tokens;
    // Create input stream from CSV content retrieved from HTTP Response
    std::istringstream input(HttpResponseContent.GetChars());
    int LineNumber = 1;

    // We're going to:
    // - loop line by line
    // - convert each line to SCString for processing
    // - remove " characters as first and last characters which cause an error
    // - tokenize the line 
    // - initialize the Tool and tool settings
    // - then process each token extracted from the current line
    for (std::string line; getline(input,line);) {
        // msg.Format("%s", line.c_str());
        // sc.AddMessageToLog(msg,1);

        SCString scline = line.c_str();
		// May be a Google Sheets specific issue, unsure
        scline = scline.GetSubString(scline.GetLength() - 2, 1);
        scline.Tokenize("\",\"", tokens);

        s_UseTool Tool;
        Tool.LineStyle = LINESTYLE_SOLID;
        Tool.LineWidth = 1;
        Tool.TextAlignment = DT_RIGHT;
        int TokenIndex = 1;
        float price;
        SCString note;
        SCString color;
        int linewidth = 1;
        int textalignment = 1;
        for (char* i : tokens) {
            //msg.Format("%s", i);
            //sc.AddMessageToLog(msg,1);

            // price
            if (TokenIndex == 1) {
                // Should technically be replaced with stof(i) for type checking
                // we control the data so "assume" type is proper
                price = atof(i);
                Tool.DrawingType = DRAWING_HORIZONTALLINE;
                Tool.BeginValue = price;
                Tool.EndValue = price;
            }

            // note
            else if (TokenIndex == 2) {
                note = i;
            }
            // color
            else if (TokenIndex == 3) {
                color = i;
				SCString cellValue(i);
				
                // Inefficient -- Should use {enum} and switch or colorMap hash
                // leaving these for future improvements to add color override.  Out of scope for initial offering to repair functionality
            
                if (color == "red") Tool.Color = COLOR_RED;
                else if (color == "green") Tool.Color = COLOR_GREEN;
                else if (color == "blue") Tool.Color = COLOR_BLUE;
                else if (color == "white") Tool.Color = COLOR_WHITE;
                else if (color == "black") Tool.Color = COLOR_BLACK;
                else if (color == "purple") Tool.Color = COLOR_PURPLE;
                else if (color == "pink") Tool.Color = COLOR_PINK;
                else if (color == "yellow") Tool.Color = COLOR_YELLOW;
                else if (color == "gold") Tool.Color = COLOR_GOLD;
                else if (color == "brown") Tool.Color = COLOR_BROWN;
                else if (color == "cyan") Tool.Color = COLOR_CYAN;
                else if (color == "gray") Tool.Color = COLOR_GRAY;
                else if (cellValue.GetLength() == 7 && cellValue[0] == '#') {
                    int r, g, b;
                    sscanf_s(cellValue.GetChars() + 1, "%02x%02x%02x", &r, &g, &b);
                    Tool.Color = RGB(r, g, b);
                } else {
                    Tool.Color = COLOR_WHITE;
                }

            }

            // draw line
            if (DrawLinesOnChart.GetInt() == 1) {
                Tool.ChartNumber = sc.ChartNumber;
                Tool.BeginDateTime = sc.BaseDateTimeIn[0];
                Tool.EndDateTime = sc.BaseDateTimeIn[sc.ArraySize-1];
                Tool.AddMethod = UTAM_ADD_OR_ADJUST;
                Tool.ShowPrice = ShowPriceOnChart.GetInt();


                // Erroneous \n chars in note cause unhandled exception
                // Purpose: To find & replace the first occurrence of an escaped newline string literal ("\\n") in a given string with an actual newline character ('\n'). 
                // It involves the following steps:
                // 1. Retrieving the string to be processed.
                // 2. Defining the escaped newline string literal for comparison.
                // 3. Searching for the literal in the string.
                // 4. If found, replacing the first backslash of the escaped newline with a newline character.
                // 5. Removing the second character ('n') of the escaped newline, adjusting the string to contain the actual newline character.

                std::string NoteStr;
                NoteStr = note.GetChars();

                std::string NewLine = "\\n";

                std::size_t IndexOfNewLine;

                char * p_substr = strstr(NoteStr.c_str(), NewLine.c_str());
                if (p_substr != NULL) {
                    IndexOfNewLine = NoteStr.find(NewLine);

                    strncpy(p_substr, "\n", 1);

                    NoteStr.erase(IndexOfNewLine + 1, 1);
                }
                Tool.Text.Format("%s", NoteStr.c_str());
                Tool.LineNumber = LineNumber;
                sc.UseTool(Tool);
            }
            
            TokenIndex++;
        }

        LineNumber++;

    }

    
}

