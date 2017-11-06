#pragma once
#include <vector>

// This class represents a sparse matrix in the row-compressed format (3-array format)
class CSRMatrix
{
public:
	// default constructor
	CSRMatrix();

	// create a matrix of given size
	CSRMatrix(int rows, int cols);

	// copy constructor
	CSRMatrix(const CSRMatrix& A);

	// Create matrix
	void create(int nr, int nc);

	// assignment operator
	void operator = (const CSRMatrix& A);

	// return row count
	int rows() const { return m_nr; }

	// return columns count
	int cols() const { return m_nc; }

	// set the value, inserting it if necessary
	void set(int i, int j, double val);

	// get a value
	double operator () (int i, int j) const;

	// see if a matrix entry was allocated
	bool isAlloc(int i, int j) const;

public:
	// matrix addition
	CSRMatrix operator + (const CSRMatrix& A);

	// matrix subtraction
	CSRMatrix operator - (const CSRMatrix& A);

	// vector-matrix product
	std::vector<double> operator * (const std::vector<double>& a);

	// more efficient multiplication: A.x = r
	void multv(const std::vector<double>& x, std::vector<double>& r);
	void multv(const double* x, double* r);

	// normalize the matrix 
	void normalize(const std::vector<double>& l, const std::vector<double>& r);

public:
	std::vector<double>& values() { return m_values; }
	std::vector<int>& indices() { return m_columns; }
	std::vector<int>& pointers() { return m_rowIndex; }

private:
	int		m_nr;		// number of rows
	int		m_nc;		// number of columns
	std::vector<int>	m_rowIndex;		// start of row in columns array
	std::vector<int>	m_columns;		// columns of non-zero entries
	std::vector<double>	m_values;		// values of matrix
};