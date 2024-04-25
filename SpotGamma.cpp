// The stdlib includes are included for us in the SierraChart "remote build"
// #include <iostream>
// #include <string>
#include "sierrachart.h"

SCDLLName("SpotGamma")

const SCString FAKE_URL = "https://spotgamma-system-files.s3.amazonaws.com/ENTER_VALID_URL.csv";

/**************
 * Constants  *
 **************/

enum {REQUEST_NOT_SENT = 0,  REQUEST_SENT};

/*********************
 * Helper Functions  *
 *********************/

void cleanDrawings(int lineCountStart, SCStudyInterfaceRef sc) {
  // clear all drawings
  int _lineNumber = lineCountStart;
  while (true) {
    if (!sc.ChartDrawingExists(sc.ChartNumber, _lineNumber)) {
      break;
    }

    sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, _lineNumber);
    _lineNumber++;
  }
}

void drawPriceLevel(
    SCDateTime beginTime,
    float price,
    int lineNumber,
    int color,
    int width,
    SCString label,
    int fontSize,
    SCStudyInterfaceRef sc,
    SubgraphLineStyles lineStyle = LINESTYLE_SOLID)
{

  SCDateTime endTime = sc.GetTradingDayStartDateTimeOfBar(beginTime) + SCDateTime::DAYS(3);

  s_UseTool Tool;

  // SCInputRef TextPosition = sc.Input[2];

  const bool isUserDrawing = false;

  Tool.Clear();
  Tool.ChartNumber = sc.ChartNumber;
  Tool.Region = sc.GraphRegion;
  Tool.DrawingType = DRAWING_LINE;
  Tool.BeginDateTime = beginTime;
  Tool.EndDateTime = endTime;
  Tool.BeginValue = price;
  Tool.EndValue = price;
  Tool.LineNumber = lineNumber;
  Tool.LineStyle = lineStyle;
  Tool.LineWidth = width;
  Tool.Color = color;
  Tool.AddAsUserDrawnDrawing = isUserDrawing;
  Tool.TextAlignment = DT_BOTTOM;
  Tool.FontSize = fontSize;
  Tool.Text = label;
  Tool.ShowPrice = 0;
  Tool.AddMethod = UTAM_ADD_OR_ADJUST;
  Tool.AllowSaveToChartbook = 0;

  sc.UseTool(Tool);
}

/***********************
 *  Exported Function  *
 ***********************/

SCSFExport scsf_SpotGamma(SCStudyInterfaceRef sc)
{
  SCInputRef UrlPath = sc.Input[0];
  SCInputRef UpdateInterval = sc.Input[1];
  SCInputRef ColorOverride = sc.Input[3];
  SCInputRef FontSize = sc.Input[4];
  SCInputRef LineWidth = sc.Input[5];

  if (sc.SetDefaults)
  {
    sc.GraphName = "SpotGamma";
    sc.GraphRegion = 0;
    sc.AutoLoop = 1;

    UrlPath.Name = "URL Path to CSV";
    UrlPath.SetString(FAKE_URL);

    UpdateInterval.Name = "Update Interval In Minutes";
    UpdateInterval.SetInt(5);

    ColorOverride.Name = "Color Override";
    ColorOverride.SetColor(RGB(0, 0, 0));

    FontSize.Name = "Font Size";
    FontSize.SetInt(14);

    LineWidth.Name = "Line Width";
    LineWidth.SetInt(1);

    return;
  }

  const int lineCountStart = 2020;
  SCDateTime &lastUpdate = sc.GetPersistentSCDateTime(0);
  int &downloadingStatus = sc.GetPersistentInt(1);
  int &callsSinceRequestSent = sc.GetPersistentInt(2);

  // Reset
  if (sc.Index == 0) {
    lastUpdate = 0;
    downloadingStatus = 0;
    callsSinceRequestSent = 0;
  }

  if (sc.IsFullRecalculation || sc.HideStudy) {
    return;
  }

  // Reset downloading status if request has taken too long
  // This is more or less an arbritriary number
  if (callsSinceRequestSent > 500) {
    downloadingStatus = REQUEST_NOT_SENT;
    sc.AddMessageToLog("Request marked as failed", 0);
  }

  if (downloadingStatus == REQUEST_SENT) {
    callsSinceRequestSent++;

    //The request has not completed, therefore there is nothing to do so we will return
    if (sc.HTTPResponse == "") {
      return;
    }
  }

  // Only run at time interval
  SCDateTime Now = sc.CurrentSystemDateTime;
  SCDateTime lastUpdateCopy = lastUpdate;
  if (lastUpdate != 0 && Now < lastUpdateCopy.AddMinutes(UpdateInterval.GetInt()) && downloadingStatus != REQUEST_SENT) {
    return;
  }
  lastUpdate = Now;

  const SCString urlPath = UrlPath.GetString();
  if (urlPath == FAKE_URL) {
    sc.AddMessageToLog("Please setup the study.", 0);
    return;
  }

  // Download CSV only if we haven't already sent out the request
  if (downloadingStatus == REQUEST_NOT_SENT) {
    if (!sc.MakeHTTPRequest(urlPath)) {
      sc.AddMessageToLog("Error making HTTP request.", 0);
      downloadingStatus = REQUEST_NOT_SENT;
      return;
    } else {
      downloadingStatus = REQUEST_SENT;
      callsSinceRequestSent = 0;
      return;
    }
  }

  // At this point the request should've been received now we must error check
  if (downloadingStatus != REQUEST_SENT) {
    sc.AddMessageToLog("Something went wrong", 0);
    return;
  } else if (sc.HTTPResponse == "ERROR") {
    sc.AddMessageToLog("There was an error while making the request. Please verify the URL provided.", 0);
    return;
  }

  // Reset state
  downloadingStatus = REQUEST_NOT_SENT;

  sc.AddMessageToLog("Downloaded Data: ", 0);
  sc.AddMessageToLog(sc.HTTPResponse, 0);

  // Clear drawings after each run
  cleanDrawings(lineCountStart, sc);

  // Parse CSV
  sc.AddMessageToLog("Adding levels", 0);

  SCString fileContents = sc.HTTPResponse;
  std::vector<char *> fileContentsVector;
  fileContents.Tokenize("\n", fileContentsVector);

  // Iterate through each line and draw it
  int lineNumber = 0;
  const int colorOverride = ColorOverride.GetColor();
  for (char *lineChars : fileContentsVector) {
    std::vector<char *> columns;
    SCString scstringLine = lineChars;

    // Prevents parsing additional lines
    if (scstringLine.GetLength() < 10) {
      sc.AddMessageToLog("Added levels", 0);
      return;
    }

    // Tokenize
    scstringLine.Tokenize(",", columns);

    int index = 0;
    float priceLevel = 0; // Column A
    char *label;          // Column B
    SCString colorString; // Column C
    for (char *cellChars : columns) {
      if (index == 0) {
        priceLevel = std::stof(cellChars);
      } else if (index == 1) {
        label = cellChars;
      } else if (index == 2) {
        SCString cellValue(cellChars);
        if (cellValue.GetLength() == 7) {
          colorString += cellValue.GetSubString(6, 1).GetChars();
        } else {
          colorString += "CCCCCC";
        }
      }

      index++;
    }

    int r, g, b;
    sscanf_s(colorString.GetChars(), "%02x%02x%02x", &r, &g, &b);
    const int color = colorOverride != RGB(0, 0, 0) ? colorOverride : RGB(r, g, b);

    if (!(priceLevel > 0)) {
      return;
    }

    SCDateTime startTime = sc.BaseDateTimeIn[sc.Index];
    startTime.SetTimeHMS(0, 0, 0);
    drawPriceLevel(startTime, priceLevel, lineCountStart + lineNumber, color, LineWidth.GetInt(), label, FontSize.GetInt(), sc);

    lineNumber++;
  }

  sc.AddMessageToLog("Added levels", 0);
}
