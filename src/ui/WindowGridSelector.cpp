#include "WindowGridSelector.h"
#include "GridCell.h"
#include "UIHelpers.h"

// WindowGridSelector - takes monitor index in constructor
WindowGridSelector::WindowGridSelector(int monitorIndex, QWidget *parent)
    : QDialog(parent), m_monitorIndex(monitorIndex) {
  auto screens = QGuiApplication::screens();
  if (m_monitorIndex >= screens.size())
    m_monitorIndex = 0;

  // Frameless window with rounded corners (matching app style)
  setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
  setAttribute(Qt::WA_TranslucentBackground);
  setMinimumSize(500, 500);

  setupUI();
  rebuildGrid();
  updatePreview();
}

void WindowGridSelector::setupUI() {
  // Outer layout for transparent dialog
  auto *outerLayout = new QVBoxLayout(this);
  outerLayout->setContentsMargins(0, 0, 0, 0);

  // Background container with window background role
  auto *bgContainer = new QWidget();
  UIHelpers::applyWindowBackgroundRole(bgContainer);
  outerLayout->addWidget(bgContainer);

  auto *mainLayout = new QVBoxLayout(bgContainer);
  mainLayout->setContentsMargins(16, 12, 16, 16);
  mainLayout->setSpacing(10);

  // === Custom Title Bar ===
  auto screens = QGuiApplication::screens();
  QRect geo = screens[m_monitorIndex]->geometry();
  QString titleText = QString("Select Position - Monitor %1 (%2x%3)")
                          .arg(m_monitorIndex + 1)
                          .arg(geo.width())
                          .arg(geo.height());
  auto *titleBar = UIHelpers::createTitleBar(bgContainer, titleText, "layout",
                                             [this]() { reject(); });
  mainLayout->addWidget(titleBar);

  // === Monitor Info ===
  QRect avail = screens[m_monitorIndex]->availableGeometry();

  // Detect taskbar position
  QString taskbarPos = "none";
  if (avail.y() > geo.y())
    taskbarPos = "top";
  else if (avail.height() < geo.height())
    taskbarPos = "bottom";
  else if (avail.x() > geo.x())
    taskbarPos = "left";
  else if (avail.width() < geo.width())
    taskbarPos = "right";

  m_monitorInfoLabel =
      new QLabel(QString("Monitor %1: %2x%3 | Usable: %4x%5 (taskbar: %6)")
                     .arg(m_monitorIndex + 1)
                     .arg(geo.width())
                     .arg(geo.height())
                     .arg(avail.width())
                     .arg(avail.height())
                     .arg(taskbarPos));
  UIHelpers::applyGoldColorRole(m_monitorInfoLabel);
  m_monitorInfoLabel->setAlignment(Qt::AlignCenter);
  mainLayout->addWidget(m_monitorInfoLabel);

  // === Grid Size Selection ===
  auto *gridRow = new QHBoxLayout();
  gridRow->addStretch();
  gridRow->addWidget(new QLabel("Grid:"));
  m_gridCombo = new QComboBox();

  // GW2 minimum window size
  const int GW2_MIN_WIDTH = 800;
  const int GW2_MIN_HEIGHT = 600;

  // Define available grids (practical sizes)
  struct GridOption {
    int cols;
    int rows;
    QString label;
  };
  QList<GridOption> allGrids = {
      {1, 1, "1 x 1 (Full)"}, {2, 1, "2 x 1"}, {2, 2, "2 x 2"}, {3, 2, "3 x 2"},
      {3, 3, "3 x 3"},        {4, 3, "4 x 3"}, {4, 4, "4 x 4"}, {6, 4, "6 x 4"},
      {6, 6, "6 x 6"},        {8, 6, "8 x 6"}};

  // First pass: Add recommended grids (cells >= 800x600)
  int defaultIndex = 0; // Will be updated based on current position
  for (const auto &g : allGrids) {
    int cellW = avail.width() / g.cols;
    int cellH = avail.height() / g.rows;

    if (cellW >= GW2_MIN_WIDTH && cellH >= GW2_MIN_HEIGHT) {
      m_gridCombo->addItem(
          UIHelpers::themedIcon("check-green"),
          QString("%1 (%2x%3)").arg(g.label).arg(cellW).arg(cellH),
          QSize(g.cols, g.rows));
    }
  }

  // Second pass: Add warning grids (400-799px range)
  bool addedWarning = false;
  for (const auto &g : allGrids) {
    int cellW = avail.width() / g.cols;
    int cellH = avail.height() / g.rows;

    if (!(cellW >= GW2_MIN_WIDTH && cellH >= GW2_MIN_HEIGHT) && cellW >= 400 &&
        cellH >= 300) {
      if (!addedWarning) {
        m_gridCombo->insertSeparator(m_gridCombo->count());
        addedWarning = true;
      }
      m_gridCombo->addItem(
          UIHelpers::themedIcon("alert-yellow"),
          QString("%1 (%2x%3)").arg(g.label).arg(cellW).arg(cellH),
          QSize(g.cols, g.rows));
    }
  }

  // Third pass: Add tiny grids (< 400px) - recommend multi-cell selection
  bool addedTiny = false;
  for (const auto &g : allGrids) {
    int cellW = avail.width() / g.cols;
    int cellH = avail.height() / g.rows;

    if (cellW < 400 || cellH < 300) {
      if (cellW >= 200 && cellH >= 150) { // Still somewhat usable
        if (!addedTiny) {
          m_gridCombo->insertSeparator(m_gridCombo->count());
          addedTiny = true;
        }
        m_gridCombo->addItem(
            UIHelpers::themedIcon("alert-red"),
            QString("%1 (%2x%3)").arg(g.label).arg(cellW).arg(cellH),
            QSize(g.cols, g.rows));
      }
    }
  }

  if (m_gridCombo->count() == 0) {
    m_gridCombo->addItem("1 x 1 (Full)", QSize(1, 1));
  }

  m_gridCombo->setCurrentIndex(defaultIndex);
  connect(m_gridCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &WindowGridSelector::rebuildGrid);
  gridRow->addWidget(m_gridCombo);
  gridRow->addStretch();
  mainLayout->addLayout(gridRow);

  // Note about grid options (icons are: green check, yellow warning, red alert)
  auto *minNote = new QLabel(
      "Green = Recommended  |  Yellow = Small  |  Red = Select multiple");
  UIHelpers::applyHintRole(minNote);
  minNote->setAlignment(Qt::AlignCenter);
  mainLayout->addWidget(minNote);

  // === Quick Preset Buttons - Row 1 ===
  auto *presetRow1 = new QHBoxLayout();
  auto *fullBtn = new QPushButton("Full");
  connect(fullBtn, &QPushButton::clicked, this,
          &WindowGridSelector::selectFullScreen);
  presetRow1->addWidget(fullBtn);

  auto *leftBtn = new QPushButton("Left");
  connect(leftBtn, &QPushButton::clicked, this,
          &WindowGridSelector::selectLeftHalf);
  presetRow1->addWidget(leftBtn);

  auto *rightBtn = new QPushButton("Right");
  connect(rightBtn, &QPushButton::clicked, this,
          &WindowGridSelector::selectRightHalf);
  presetRow1->addWidget(rightBtn);

  auto *centerBtn = new QPushButton("Center");
  connect(centerBtn, &QPushButton::clicked, this,
          &WindowGridSelector::selectCenter);
  presetRow1->addWidget(centerBtn);
  mainLayout->addLayout(presetRow1);

  // === Quick Preset Buttons - Row 2 ===
  auto *presetRow2 = new QHBoxLayout();
  auto *topLeftBtn = new QPushButton("Top-L");
  connect(topLeftBtn, &QPushButton::clicked, this,
          &WindowGridSelector::selectTopLeft);
  presetRow2->addWidget(topLeftBtn);

  auto *topRightBtn = new QPushButton("Top-R");
  connect(topRightBtn, &QPushButton::clicked, this,
          &WindowGridSelector::selectTopRight);
  presetRow2->addWidget(topRightBtn);

  auto *bottomLeftBtn = new QPushButton("Bot-L");
  connect(bottomLeftBtn, &QPushButton::clicked, this,
          &WindowGridSelector::selectBottomLeft);
  presetRow2->addWidget(bottomLeftBtn);

  auto *bottomRightBtn = new QPushButton("Bot-R");
  connect(bottomRightBtn, &QPushButton::clicked, this,
          &WindowGridSelector::selectBottomRight);
  presetRow2->addWidget(bottomRightBtn);
  mainLayout->addLayout(presetRow2);

  // === Grid Container (proportional to monitor) ===
  m_gridContainer = new QWidget();
  m_gridLayout = new QGridLayout(m_gridContainer);
  m_gridLayout->setSpacing(2);
  m_gridLayout->setContentsMargins(0, 0, 0, 0);

  // Make container proportional to monitor aspect ratio
  auto screens2 = QGuiApplication::screens();
  QRect monGeo = screens2[m_monitorIndex]->geometry();
  double aspect = (double)monGeo.width() / monGeo.height();
  // Base height 200, width proportional
  int baseHeight = 200;
  int baseWidth = (int)(baseHeight * aspect);
  m_gridContainer->setMinimumSize(baseWidth, baseHeight);
  m_gridContainer->setMaximumHeight(300);

  mainLayout->addWidget(m_gridContainer, 1);

  // === Preview Label ===
  m_previewLabel = new QLabel("Select cells to preview window size");
  m_previewLabel->setAlignment(Qt::AlignCenter);
  UIHelpers::applyStatusRole(m_previewLabel);
  mainLayout->addWidget(m_previewLabel);

  // === Selection Instructions ===
  auto *instructLabel = new QLabel(
      "Click = Select cell  |  Shift+Click = Select rectangle\n"
      "Red icon = Very small cells, select multiple for usable size");
  UIHelpers::applyHintRole(instructLabel);
  mainLayout->addWidget(instructLabel);

  // === Buttons ===
  auto *buttonRow = new QHBoxLayout();
  buttonRow->addStretch();

  auto *cancelBtn = new QPushButton("Cancel");
  UIHelpers::applyCancelStyle(cancelBtn);
  connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
  buttonRow->addWidget(cancelBtn);

  auto *applyBtn = new QPushButton("Apply");
  UIHelpers::applyPrimaryStyle(applyBtn);
  connect(applyBtn, &QPushButton::clicked, this, &WindowGridSelector::onApply);
  buttonRow->addWidget(applyBtn);

  mainLayout->addLayout(buttonRow);
}

void WindowGridSelector::setCurrentPosition(QRect currentRect) {
  m_currentRect = currentRect;

  // Try to select a grid that best matches the current position
  if (currentRect.isValid() && !currentRect.isEmpty()) {
    auto screens = QGuiApplication::screens();
    if (m_monitorIndex < screens.size()) {
      QRect avail = screens[m_monitorIndex]->availableGeometry();

      // Find grid where position aligns best with cell boundaries
      // Prefer grid with smallest alignment error
      int bestIndex = 0;
      int bestError = INT_MAX;

      for (int i = 0; i < m_gridCombo->count(); i++) {
        QSize gridSize = m_gridCombo->itemData(i).toSize();
        if (!gridSize.isValid())
          continue; // Skip separators

        int cols = gridSize.width();
        int rows = gridSize.height();
        int cellW = avail.width() / cols;
        int cellH = avail.height() / rows;

        if (cellW == 0 || cellH == 0)
          continue;

        // Check how well position aligns with grid
        int xError = std::abs((currentRect.x() - avail.x()) % cellW);
        int yError = std::abs((currentRect.y() - avail.y()) % cellH);
        int wError = std::abs(currentRect.width() % cellW);
        int hError = std::abs(currentRect.height() % cellH);

        int totalError = xError + yError + wError + hError;

        // Prefer exact alignment (error = 0)
        if (totalError < bestError) {
          bestError = totalError;
          bestIndex = i;
        }
      }

      m_gridCombo->setCurrentIndex(bestIndex);
    }
  }

  // Rebuild grid to show current position as gold/gray cells
  rebuildGrid();
}

void WindowGridSelector::preselectCurrentPosition() {
  if (!m_currentRect.isValid() || m_currentRect.isEmpty())
    return;

  auto screens = QGuiApplication::screens();
  if (m_monitorIndex >= screens.size())
    return;

  // Find which monitor the current rect is on
  int currentMonitor = -1;
  for (int i = 0; i < screens.size(); i++) {
    if (screens[i]->availableGeometry().contains(m_currentRect.topLeft())) {
      currentMonitor = i;
      break;
    }
  }

  if (currentMonitor < 0)
    return; // Not on any monitor

  bool isOnThisMonitor = (currentMonitor == m_monitorIndex);
  QRect refGeo = screens[currentMonitor]->availableGeometry();

  QSize gridSize = m_gridCombo->currentData().toSize();
  int cols = gridSize.width();
  int rows = gridSize.height();

  int cellW = refGeo.width() / cols;
  int cellH = refGeo.height() / rows;

  // Calculate which cells match the current position
  int startCol = (m_currentRect.x() - refGeo.x()) / cellW;
  int startRow = (m_currentRect.y() - refGeo.y()) / cellH;
  int endCol = startCol + (m_currentRect.width() / cellW) - 1;
  int endRow = startRow + (m_currentRect.height() / cellH) - 1;

  // Clamp
  startCol = (std::max)(0, (std::min)(startCol, cols - 1));
  endCol = (std::max)(0, (std::min)(endCol, cols - 1));
  startRow = (std::max)(0, (std::min)(startRow, rows - 1));
  endRow = (std::max)(0, (std::min)(endRow, rows - 1));

  if (startCol <= endCol && startRow <= endRow) {
    for (auto *cell : m_cells) {
      if (cell->row() >= startRow && cell->row() <= endRow &&
          cell->col() >= startCol && cell->col() <= endCol) {
        if (isOnThisMonitor) {
          // Gold for current selection on this monitor
          cell->setSelected(true);
          m_selectedCells.insert({cell->row(), cell->col()});
        } else {
          // Gray for current selection on different monitor
          cell->setCurrent(true);
        }
      }
    }
  }
}

void WindowGridSelector::rebuildGrid() {
  for (auto *cell : m_cells) {
    m_gridLayout->removeWidget(cell);
    delete cell;
  }
  m_cells.clear();
  m_selectedCells.clear();

  QSize gridSize = m_gridCombo->currentData().toSize();
  int cols = gridSize.width();
  int rows = gridSize.height();

  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++) {
      auto *cell = new GridCell(r, c, m_gridContainer);
      connect(cell, &GridCell::clicked, this,
              &WindowGridSelector::onCellClicked);
      m_gridLayout->addWidget(cell, r, c);
      m_cells.append(cell);
    }
  }

  preselectCurrentPosition();
  updatePreview();
}

void WindowGridSelector::onCellClicked(int row, int col,
                                       Qt::KeyboardModifiers mods) {
  if (mods & Qt::ShiftModifier && !m_selectedCells.empty()) {
    selectRectangle(m_lastClickRow, m_lastClickCol, row, col);
  } else {
    clearSelection();
    selectCell(row, col);
  }

  m_lastClickRow = row;
  m_lastClickCol = col;
  updatePreview();
}

void WindowGridSelector::selectCell(int row, int col) {
  for (auto *cell : m_cells) {
    if (cell->row() == row && cell->col() == col) {
      cell->setSelected(true);
      m_selectedCells.insert({row, col});
      break;
    }
  }
}

void WindowGridSelector::selectRectangle(int r1, int c1, int r2, int c2) {
  clearSelection();
  int minR = (std::min)(r1, r2), maxR = (std::max)(r1, r2);
  int minC = (std::min)(c1, c2), maxC = (std::max)(c1, c2);

  for (auto *cell : m_cells) {
    if (cell->row() >= minR && cell->row() <= maxR && cell->col() >= minC &&
        cell->col() <= maxC) {
      cell->setSelected(true);
      m_selectedCells.insert({cell->row(), cell->col()});
    }
  }
}

void WindowGridSelector::clearSelection() {
  for (auto *cell : m_cells) {
    cell->setSelected(false);
  }
  m_selectedCells.clear();
}

void WindowGridSelector::updatePreview() {
  if (m_selectedCells.empty()) {
    m_previewLabel->setText("Click cells to select window region");
    m_selectedRect = QRect();
    return;
  }

  auto screens = QGuiApplication::screens();
  if (m_monitorIndex >= screens.size())
    return;

  // Use available geometry (excludes taskbar) for calculations
  QRect availableGeo = screens[m_monitorIndex]->availableGeometry();

  QSize gridSize = m_gridCombo->currentData().toSize();
  int cols = gridSize.width();
  int rows = gridSize.height();

  // Cell size based on available area
  int cellW = availableGeo.width() / cols;
  int cellH = availableGeo.height() / rows;

  int minR = INT_MAX, maxR = 0, minC = INT_MAX, maxC = 0;
  for (const auto &[r, c] : m_selectedCells) {
    minR = (std::min)(minR, r);
    maxR = (std::max)(maxR, r);
    minC = (std::min)(minC, c);
    maxC = (std::max)(maxC, c);
  }

  // Window position and size = exactly the selected cells
  // Window fits WITHIN the selected area (no overflow)
  int x = availableGeo.x() + minC * cellW;
  int y = availableGeo.y() + minR * cellH;
  int w = (maxC - minC + 1) * cellW;
  int h = (maxR - minR + 1) * cellH;

  // Store the window position/size
  m_selectedRect = QRect(x, y, w, h);

  // Get frame info for display only
  int titleBar = GetSystemMetrics(SM_CYCAPTION);
  int border =
      GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
  int contentW = w - (border * 2);
  int contentH = h - titleBar - border;

  m_previewLabel->setText(QString("Window: %1x%2 at (%3,%4)  |  Game: ~%5x%6")
                              .arg(w)
                              .arg(h)
                              .arg(x)
                              .arg(y)
                              .arg(contentW)
                              .arg(contentH));
}

// Preset methods
void WindowGridSelector::selectFullScreen() {
  QSize gridSize = m_gridCombo->currentData().toSize();
  selectRectangle(0, 0, gridSize.height() - 1, gridSize.width() - 1);
  updatePreview();
}

void WindowGridSelector::selectLeftHalf() {
  QSize gridSize = m_gridCombo->currentData().toSize();
  int cols = gridSize.width();
  int rows = gridSize.height();
  // Left half: columns 0 to (cols/2 - 1), or at least column 0
  int endCol = (cols > 1) ? (cols - 1) / 2 : 0;
  selectRectangle(0, 0, rows - 1, endCol);
  updatePreview();
}

void WindowGridSelector::selectRightHalf() {
  QSize gridSize = m_gridCombo->currentData().toSize();
  int cols = gridSize.width();
  int rows = gridSize.height();
  // Right half: from middle to end
  int startCol = cols / 2;
  if (startCol >= cols)
    startCol = cols - 1;
  selectRectangle(0, startCol, rows - 1, cols - 1);
  updatePreview();
}

void WindowGridSelector::selectTopLeft() {
  QSize gridSize = m_gridCombo->currentData().toSize();
  int cols = gridSize.width();
  int rows = gridSize.height();
  // Top-left quadrant: from (0,0) to (halfRows-1, halfCols-1)
  int endRow = (rows - 1) / 2;
  int endCol = (cols - 1) / 2;
  selectRectangle(0, 0, endRow, endCol);
  updatePreview();
}

void WindowGridSelector::selectTopRight() {
  QSize gridSize = m_gridCombo->currentData().toSize();
  int cols = gridSize.width();
  int rows = gridSize.height();
  // Top-right quadrant: from (0, halfCols) to (halfRows-1, cols-1)
  int endRow = (rows - 1) / 2;
  int startCol = cols / 2;
  selectRectangle(0, startCol, endRow, cols - 1);
  updatePreview();
}

void WindowGridSelector::selectBottomLeft() {
  QSize gridSize = m_gridCombo->currentData().toSize();
  int cols = gridSize.width();
  int rows = gridSize.height();
  // Bottom-left quadrant: from (halfRows, 0) to (rows-1, halfCols-1)
  int startRow = rows / 2;
  int endCol = (cols - 1) / 2;
  selectRectangle(startRow, 0, rows - 1, endCol);
  updatePreview();
}

void WindowGridSelector::selectBottomRight() {
  QSize gridSize = m_gridCombo->currentData().toSize();
  int cols = gridSize.width();
  int rows = gridSize.height();
  // Bottom-right quadrant: from (halfRows, halfCols) to (rows-1, cols-1)
  int startRow = rows / 2;
  int startCol = cols / 2;
  selectRectangle(startRow, startCol, rows - 1, cols - 1);
  updatePreview();
}

void WindowGridSelector::selectCenter() {
  QSize gridSize = m_gridCombo->currentData().toSize();
  int cols = gridSize.width();
  int rows = gridSize.height();

  // Center: middle cells (if grid is large enough)
  // For small grids, just select the center cell(s)
  int startCol = cols / 3;
  int endCol = cols - 1 - startCol;
  int startRow = rows / 3;
  int endRow = rows - 1 - startRow;

  // Ensure valid range
  if (startCol > endCol) {
    startCol = cols / 2;
    endCol = cols / 2;
  }
  if (startRow > endRow) {
    startRow = rows / 2;
    endRow = rows / 2;
  }

  selectRectangle(startRow, startCol, endRow, endCol);
  updatePreview();
}

void WindowGridSelector::onApply() {
  if (!m_selectedRect.isEmpty()) {
    emit positionSelected(m_selectedRect);
    accept();
  }
}

void WindowGridSelector::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    // Only allow drag from title bar area (top 50 pixels)
    if (event->position().y() < 50) {
      m_dragPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
      m_dragging = true;
      event->accept();
    } else {
      m_dragging = false;
    }
  }
}

void WindowGridSelector::mouseMoveEvent(QMouseEvent *event) {
  if (m_dragging && (event->buttons() & Qt::LeftButton)) {
    move(event->globalPosition().toPoint() - m_dragPos);
    event->accept();
  }
}

void WindowGridSelector::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    m_dragging = false;
  }
  QDialog::mouseReleaseEvent(event);
}
